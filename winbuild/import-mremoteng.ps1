<#
.SYNOPSIS
    Imports mRemoteNG connections into KRDC (krdc-ai) favorites.

.DESCRIPTION
    Reads mRemoteNG's confCons.xml connection tree and writes it into KRDC's
    bookmark file (XBEL) as a folder group, mirroring the mRemoteNG folder
    hierarchy. Passwords are deliberately never read: KRDC asks once on the
    first connect and remembers the password in the Windows credential store.

    - RDP connections become rdp://[user@]host[:port][?domain=...] bookmarks,
      the exact href form KRDC itself uses.
    - mRemoteNG containers become KRDC folders.
    - VNC can be included with -IncludeVNC (works on the Linux krdc-ai build;
      the Windows portable build is RDP-only, so those entries would sit inert
      there). Everything else (SSH, Telnet, HTTP, ...) is reported as skipped.
    - Inherited username/domain/port from parent containers are resolved.
    - Existing bookmarks are kept; connections already present (same href) are
      skipped, so re-running just imports what changed.
    - A timestamped backup of bookmarks.xml is written next to it.

    KRDC must not be running: it rewrites bookmarks.xml on exit and would
    clobber the import.

.EXAMPLE
    Unblock-File .\import-mremoteng.ps1
    powershell -File .\import-mremoteng.ps1
    Default paths: %APPDATA%\mRemoteNG\confCons.xml -> %LOCALAPPDATA%\krdc\bookmarks.xml

.EXAMPLE
    powershell -File .\import-mremoteng.ps1 -IncludeVNC -ImportFolderName "From mRemoteNG"
    (Unblock-File first clears the downloaded-from-internet mark on this file.)
#>
[CmdletBinding()]
param(
    # mRemoteNG connection file (File > Settings shows the configured path).
    [string]$ConfConsPath,
    # KRDC bookmark file. This is where KRDC looks on Windows
    # (QStandardPaths::GenericDataLocation resolves to %LOCALAPPDATA%).
    [string]$BookmarksPath,
    # Name of the top-level folder the import is placed in.
    [string]$ImportFolderName = 'mRemoteNG import',
    # Also import VNC connections (skip on the RDP-only Windows build).
    [switch]$IncludeVNC,
    # Import even while krdc.exe is running (not recommended).
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not $ConfConsPath) {
    $ConfConsPath = Join-Path $env:APPDATA 'mRemoteNG\confCons.xml'
}
if (-not $BookmarksPath) {
    $BookmarksPath = Join-Path $env:LOCALAPPDATA 'krdc\bookmarks.xml'
}

# --- preflight ---------------------------------------------------------------

if (-not (Test-Path -LiteralPath $ConfConsPath)) {
    Write-Error "mRemoteNG config not found: $ConfConsPath (pass -ConfConsPath if yours lives elsewhere)"
}
$running = Get-Process -Name 'krdc' -ErrorAction SilentlyContinue
if ($running -and -not $Force) {
    Write-Error 'krdc.exe is running - close it first (it overwrites bookmarks.xml on exit), or pass -Force.'
}

$confText = Get-Content -LiteralPath $ConfConsPath -Raw -Encoding UTF8
if ($confText -notmatch '<Connections') {
    Write-Error "confCons.xml does not look like plain XML. If you set an mRemoteNG master password, temporarily clear it (Tools > Advanced), export, then re-run."
}
$conf = New-Object System.Xml.XmlDocument
$conf.LoadXml($confText)

# --- collect connections, resolving inherited fields down the tree ------------

$conns = New-Object System.Collections.ArrayList
$skipped = @{}

function Add-FromNode($node, $folderPath, $inherited, $isRoot) {
    $type = $node.GetAttribute('type')
    if ($type -ne 'Connection' -and $type -ne 'Container') { return }

    # username/domain/port inheritance from the enclosing container:
    # inheritX=True means "take the parent's value", otherwise the node's own
    # attribute wins even when empty (mRemoteNG semantics).
    $vals = @{}
    $vals['user']   = $inherited['user']
    $vals['domain'] = $inherited['domain']
    $vals['port']   = $inherited['port']
    if ($node.GetAttribute('inheritusername') -ne 'True') { $vals['user'] = $node.GetAttribute('username') }
    if ($node.GetAttribute('inheritdomain') -ne 'True')   { $vals['domain'] = $node.GetAttribute('domain') }
    if ($node.GetAttribute('inheritport') -ne 'True')     { $vals['port'] = $node.GetAttribute('port') }

    if ($type -eq 'Container') {
        # The outermost mRemoteNG container ("Conns") is just the tree root;
        # don't create a folder for it, but still pass its inherited fields down.
        $childPath = $folderPath
        if (-not $isRoot) {
            $name = $node.GetAttribute('name')
            if (-not $name) { $name = '(unnamed folder)' }
            $childPath = if ($folderPath) { $folderPath + '\' + $name } else { $name }
        }
        foreach ($child in (childNodes $node)) { Add-FromNode $child $childPath $vals $false }
        return
    }

    $protocol = $node.GetAttribute('protocol')
    $scheme = $null
    if ($protocol -eq 'RDP') { $scheme = 'rdp' }
    elseif ($protocol -eq 'VNC' -and $IncludeVNC) { $scheme = 'vnc' }
    if (-not $scheme) {
        $key = if ($protocol) { $protocol } else { 'Unknown' }
        if ($skipped.ContainsKey($key)) { $skipped[$key] = $skipped[$key] + 1 } else { $skipped[$key] = 1 }
        return
    }

    $host_ = $node.GetAttribute('hostname')
    if (-not $host_) { return }  # nothing to dial
    $name = $node.GetAttribute('name')
    if (-not $name) { $name = $host_ }

    $href = $scheme + '://'
    $user = $vals['user']
    if ($user) { $href += [System.Uri]::EscapeDataString($user) + '@' }
    $href += $host_
    $port = $vals['port']
    # omit the default port, matching the hrefs KRDC itself writes
    if ($port -and $port -ne '' -and $port -ne '3389' -and $port -ne '5900') {
        $href += ':' + $port
    }
    if ($vals['domain']) { $href += '?domain=' + [System.Uri]::EscapeDataString($vals['domain']) }

    [void]$conns.Add([pscustomobject]@{ Name = $name; Href = $href; Folder = $folderPath })
}

function childNodes($node) {
    # mRemoteNG nests containers under a <Nodes> wrapper element
    $wrapper = $node.SelectSingleNode('./Nodes')
    if ($wrapper) { return @($wrapper.SelectNodes('./Node')) }
    return @($node.SelectNodes('./Node'))
}

foreach ($top in childNodes $conf.DocumentElement) {
    Add-FromNode $top '' @{} $true
}

if ($conns.Count -eq 0) {
    Write-Host 'No importable connections found in confCons.xml.' -ForegroundColor Yellow
    foreach ($k in $skipped.Keys) { Write-Host ("  skipped {0}: {1}" -f $k, $skipped[$k]) }
    exit 0
}

# --- load (or create) the KRDC bookmark file ----------------------------------

$xbelNs = 'http://www.freedesktop.org/standards/desktop-bookmarks'
$kdeMetaNs = 'http://www.kde.org'

$bookmarks = New-Object System.Xml.XmlDocument
$existed = Test-Path -LiteralPath $BookmarksPath
if ($existed) {
    $raw = Get-Content -LiteralPath $BookmarksPath -Raw -Encoding UTF8
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $backup = "$BookmarksPath.bak-$stamp"
    Copy-Item -LiteralPath $BookmarksPath -Destination $backup
    Write-Host "Backed up existing bookmarks to $backup"

    # Qt's QDomDocument sometimes re-saves KRDC's bookmarks.xml with an
    # <bookmark:icon> element whose prefix was never declared (Qt parses it
    # back leniently; .NET refuses). Bind any undeclared prefix to the one
    # namespace KRDC uses, so strict tools can read these files too; the
    # rewritten file comes back well-formed.
    $declared = @{}
    foreach ($m in [regex]::Matches($raw, 'xmlns:([A-Za-z_][\w.-]*)\s*=')) { $declared[$m.Groups[1].Value] = $true }
    $inject = ''
    $seenPrefix = @{}
    foreach ($m in [regex]::Matches($raw, '</?\s*([A-Za-z_][\w.-]*):')) {
        $p = $m.Groups[1].Value
        if ($declared.ContainsKey($p) -or $seenPrefix.ContainsKey($p)) { continue }
        $seenPrefix[$p] = $true
        if ($p -notmatch '^xml(\d|$)') {
            $inject += ' xmlns:{0}="http://www.freedesktop.org/standards/desktop-bookmarks"' -f $p
        }
    }
    if ($inject) {
        $raw = [regex]::new('<xbel(?=[\s>])').Replace($raw, ('<xbel' + $inject), 1)
        Write-Host 'Repaired undeclared XML namespace prefix(es) emitted by KRDC.'
    }
    $bookmarks.LoadXml($raw)
} else {
    $root = $bookmarks.CreateElement('xbel')
    [void]$bookmarks.AppendChild($root)
    $decl = $bookmarks.CreateXmlDeclaration('1.0', 'UTF-8', $null)
    [void]$bookmarks.InsertBefore($decl, $root)
    $dir = Split-Path -Parent $BookmarksPath
    if (-not (Test-Path -LiteralPath $dir)) { [void][System.IO.Directory]::CreateDirectory($dir) }
    Write-Host "No bookmarks.xml yet; creating $BookmarksPath"
}

$xbel = $bookmarks.DocumentElement

# --- helpers to build XBEL nodes in the shapes KRDC writes ---------------------

function New-BookmarkNode($title, $href) {
    $bm = $bookmarks.CreateElement('bookmark')
    $bm.SetAttribute('href', $href)
    $t = $bookmarks.CreateElement('title'); $t.InnerText = $title
    [void]$bm.AppendChild($t)

    $info = $bookmarks.CreateElement('info')
    $md = $bookmarks.CreateElement('metadata')
    $md.SetAttribute('owner', 'http://freedesktop.org')
    $icon = $bookmarks.CreateElement('icon', $xbelNs)   # <ns0:icon> in a fresh file
    $icon.SetAttribute('name', 'network-server')
    [void]$md.AppendChild($icon)
    [void]$info.AppendChild($md)

    $md2 = $bookmarks.CreateElement('metadata')
    $md2.SetAttribute('owner', $kdeMetaNs)
    $ta = $bookmarks.CreateElement('time_added')
    $ta.InnerText = [string][int]([DateTimeOffset]::UtcNow.ToUnixTimeSeconds())
    [void]$md2.AppendChild($ta)
    [void]$info.AppendChild($md2)

    [void]$bm.AppendChild($info)
    return $bm
}

function Find-Folder($parent, $title) {
    foreach ($f in $parent.SelectNodes('./folder')) {
        $ft = $f.SelectSingleNode('./title')
        if ($ft -and $ft.InnerText -ceq $title) { return $f }
    }
    return $null
}

function Ensure-Folder($parent, $title) {
    $f = Find-Folder $parent $title
    if (-not $f) {
        $f = $bookmarks.CreateElement('folder')
        $t = $bookmarks.CreateElement('title'); $t.InnerText = $title
        [void]$f.AppendChild($t)
        [void]$parent.AppendChild($f)
    }
    return $f
}

# --- place the import folder right after the History folder (or append) --------

$history = $null
foreach ($f in @($xbel.SelectNodes('./folder'))) {
    # KRDC marks its history folder with a plain <krdc-history>historyfolder
    # element (no XML namespace), so match it directly.
    $h = $f.SelectSingleNode(".//krdc-history")
    if ($h -and $h.InnerText -eq 'historyfolder') { $history = $f; break }
}

$import = Find-Folder $xbel $ImportFolderName
if (-not $import) {
    $import = Ensure-Folder $xbel $ImportFolderName
    if ($history) {
        # move the new folder to sit directly after History, like KRDC's own ordering
        [void]$xbel.InsertAfter($import, $history)
    }
}

# --- write connections, deduping against everything already present ------------

$existingHrefs = @{}
foreach ($b in $bookmarks.SelectNodes('//bookmark')) {
    $h = $b.GetAttribute('href')
    if ($h) { $existingHrefs[$h.ToLowerInvariant()] = $true }
}

$imported = 0; $duplicates = 0
$folderCache = @{}
foreach ($c in $conns) {
    if ($existingHrefs.ContainsKey($c.Href.ToLowerInvariant())) { $duplicates++; continue }
    $existingHrefs[$c.Href.ToLowerInvariant()] = $true

    $parent = $import
    if ($c.Folder) {
        if (-not $folderCache.ContainsKey($c.Folder)) {
            $cur = $import
            foreach ($seg in $c.Folder.Split('\')) { $cur = Ensure-Folder $cur $seg }
            $folderCache[$c.Folder] = $cur
        }
        $parent = $folderCache[$c.Folder]
    }
    [void]$parent.AppendChild((New-BookmarkNode $c.Name $c.Href))
    $imported++
}

# --- save without a BOM (Qt's parser is fine either way, but keep it tidy) -----

$settings = New-Object System.Xml.XmlWriterSettings
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$settings.Indent = $true
$settings.IndentChars = ' '
$writer = [System.Xml.XmlWriter]::Create($BookmarksPath, $settings)
try { $bookmarks.Save($writer) } finally { $writer.Close() }

Write-Host ''
Write-Host ("Imported {0} connection(s) into '{1}' of {2}" -f $imported, $ImportFolderName, $BookmarksPath) -ForegroundColor Green
if ($duplicates)   { Write-Host "Skipped $duplicates already-present connection(s)." }
foreach ($k in $skipped.Keys) {
    Write-Host ("Skipped {0} {1} connection(s) (no KRDC equivalent{2})." -f $skipped[$k], $k, $(if ($k -eq 'VNC') { ', or -IncludeVNC not set' } else { '' }))
}
Write-Host 'Passwords were not imported by design: KRDC will ask once per host and store it in the Windows credential store.'
