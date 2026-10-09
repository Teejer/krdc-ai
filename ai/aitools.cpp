/*
    SPDX-FileCopyrightText: 2026 TJ Griffiths <teej.d.man@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "aitools.h"

#include "aikeys.h"

#include <QRegularExpression>
#include <QJsonValue>

namespace
{
QJsonObject property(const QString &type, const QString &description, const QJsonArray &allowed = QJsonArray())
{
    QJsonObject object;
    object.insert(QStringLiteral("type"), type);
    object.insert(QStringLiteral("description"), description);
    if (!allowed.isEmpty()) {
        object.insert(QStringLiteral("enum"), allowed);
    }
    return object;
}

QJsonObject function(const QString &name, const QString &description, const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject parameters;
    parameters.insert(QStringLiteral("type"), QStringLiteral("object"));
    parameters.insert(QStringLiteral("properties"), properties);
    if (!required.isEmpty()) {
        QJsonArray requiredArray;
        for (const QString &entry : required) {
            requiredArray.append(entry);
        }
        parameters.insert(QStringLiteral("required"), requiredArray);
    }

    QJsonObject object;
    object.insert(QStringLiteral("name"), name);
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("parameters"), parameters);

    return object;
}

QJsonObject wrapped(const QString &name, const QString &description, const QJsonObject &properties, const QStringList &required = {})
{
    QJsonObject tool;
    tool.insert(QStringLiteral("type"), QStringLiteral("function"));
    tool.insert(QStringLiteral("function"), function(name, description, properties, required));
    return tool;
}

// Text that means "this is going to destroy something". Typing any of these is
// worth interrupting the user for, even with auto-execute enabled.
const QStringList &destructivePatterns()
{
    static const QStringList patterns = {
        QStringLiteral(R"((^|\s)sudo(\s+\S+)*\s+rm(\s|$))"),
        QStringLiteral(R"((^|\s)rm\s+-[a-zA-Z]*(rf|fr|r|f))"),
        QStringLiteral(R"((^|\s)(del|erase|rd)\s+/)"),
        QStringLiteral(R"((^|\s)format\s+[a-z]:)"),
        QStringLiteral(R"((^|\s)(mkfs|fdisk|parted|diskpart|gdisk|cgdisk|wipefs|shred|cryptsetup)(\s|$))"),
        QStringLiteral(R"((^|\s)dd\s+if=)"),
        QStringLiteral(R"(>\s*/dev/(sd|nvme|hd|disk|mmcblk))"),
        QStringLiteral(R"((^|\s)(shutdown|reboot|poweroff|halt|init)\s)"),
        QStringLiteral(R"((^|\s)(net\s+(user|localgroup|share)|netsh|dsregcmd)(\s|$))"),
        QStringLiteral(R"((^|\s)reg(\s+(add|delete|import|load))?(\s|$))"),
        QStringLiteral(R"((^|\s)(regsvr32|rundll32|mshta|certutil|bitsadmin|installutil)(\s|$))"),
        QStringLiteral(R"((^|\s)(schtasks|sc|service)\s+(delete|create|config|stop|query)?)"),
        QStringLiteral(R"((^|\s)(taskkill|pkill|killall|kill)(\s|$))"),
        QStringLiteral(R"((^|\s)(userdel|usermod|useradd|groupdel|lpasswd|passwd)(\s|$))"),
        QStringLiteral(R"((^|\s)(iptables|firewall-cmd|ufw)(\s|$))"),
        QStringLiteral(R"((^|\s)(dnf|yum|apt|apt-get|pacman|zypper|emerge)\s+(-\S+\s+)*(remove|purge|erase|unmerge|autoremove))"),
        QStringLiteral(R"((^|\s)(Remove-Item|Del-Item|Clear-RecycleBin|Rm)(\s|$))"),
        QStringLiteral(R"(Remove-Item[^;\n]*-(Recurse|Force))"),
        QStringLiteral(R"((^|\s)(Stop-Computer|Restart-Computer|Set-MpPreference)(\s|$))"),
        QStringLiteral(R"(Invoke-(Command|Expression|WebRequest)[^;\n]*(-OutFile|-UseBasicParsing)?\s*-?)"),
        QStringLiteral(R"((curl|wget)[^;\n]*\|\s*(sudo\s+)?((ba|z|da)?sh|python))"),
        QStringLiteral(R"(powershell[^;\n]*(-(e|enc|encodedcommand)\s))"),
        QStringLiteral(R"((^|\s)chmod\s+(-[a-zA-Z]+\s+)*777\s+/)"),
        QStringLiteral(R"(:\(\)\s*\{.*\};\s*:)"), // fork bomb
        QStringLiteral(R"((^|\s)(history\s+-c|unset\s+HISTFILE|set\s+\+o\s+history))"),
        QStringLiteral(R"((^|\s)(lvm|vgremove|lvremove|pvremove|mdadm\s+--zero)(\s|$))"),
        QStringLiteral(R"((^|\s)(systemctl|service)\s+(stop|disable|mask|kill)(\s|$))"),
        QStringLiteral(R"((^|\s)(write|set-content|add-content|out-file)\s+-?(force|path)?)"),
        QStringLiteral(R"((^|\s)(git\s+push\s+--force|git\s+reset\s+--hard|git\s+clean\s+-[a-z]*f))"),
        QStringLiteral(R"((^|\s)(docker\s+(system\s+prune|rmi|volume\s+rm)|kubectl\s+delete)(\s|$))"),
        QStringLiteral(R"((^|\s)(crontab|at)\s+(-r|rm)(\s|$))"),
        QStringLiteral(R"(reg\s+add[^;\n]*(HKLM|HKEY_LOCAL_MACHINE))"),
    };
    return patterns;
}

// Key combinations that log the user out, lock the screen or kill programs.
struct DangerousChord {
    QStringList keys;
    const char *reason;
};

const QList<DangerousChord> &dangerousChords()
{
    static const QList<DangerousChord> chords = {
        {QStringList{QStringLiteral("control"), QStringLiteral("alt"), QStringLiteral("delete")},
         "opens the Windows security screen, which can log the user off"},
        {QStringList{QStringLiteral("alt"), QStringLiteral("f4")}, "closes the focused window on the remote machine"},
        {QStringList{QStringLiteral("meta"), QStringLiteral("l")}, "locks the remote machine"},
        {QStringList{QStringLiteral("meta"), QStringLiteral("d")}, "minimises every window on the remote machine"},
        {QStringList{QStringLiteral("control"), QStringLiteral("alt"), QStringLiteral("f2")}, "switches the remote virtual terminal"},
    };
    return chords;
}

QStringList normalizedChord(const QString &key, const QJsonArray &modifiers)
{
    QStringList chord = AiKeys::splitChord(key);
    for (const QJsonValue &value : modifiers) {
        chord.append(value.toString().toLower());
    }
    chord.removeAll(QString());
    chord.sort();
    return chord;
}

// The hotkey tool's "keys" argument: normally one string like "ctrl+a";
// models that do send the old array form are accepted too.
QStringList hotkeyChord(const QJsonObject &arguments)
{
    const QJsonValue value = arguments.value(QStringLiteral("keys"));
    QStringList names;
    if (value.isArray()) {
        for (const QJsonValue &entry : value.toArray()) {
            names.append(AiKeys::splitChord(entry.toString()));
        }
    } else {
        names = AiKeys::splitChord(value.toString());
    }
    names.removeAll(QString());
    names.sort();
    return names;
}

bool isDangerousChord(const QStringList &sortedChord)
{
    for (const DangerousChord &dangerous : dangerousChords()) {
        QStringList expected = dangerous.keys;
        expected.sort();
        if (expected == sortedChord) {
            return true;
        }
    }
    return false;
}

QString modifierList(const QJsonArray &modifiers)
{
    QStringList names;
    for (const QJsonValue &value : modifiers) {
        names.append(value.toString());
    }
    return names.join(QLatin1Char('+'));
}

// An array property must declare its element schema; servers that validate
// arguments reject or drop bare arrays, which is why press_key's modifiers
// never arrived and the model fell back to one keystroke at a time.
QJsonObject arrayProperty(const QString &description, const QJsonArray &allowed)
{
    QJsonObject items;
    items.insert(QStringLiteral("type"), QStringLiteral("string"));
    if (!allowed.isEmpty()) {
        items.insert(QStringLiteral("enum"), allowed);
    }

    QJsonObject object;
    object.insert(QStringLiteral("type"), QStringLiteral("array"));
    object.insert(QStringLiteral("description"), description);
    object.insert(QStringLiteral("items"), items);
    return object;
}

QString truncated(const QString &text, int limit = 70)
{
    const QString flat = QString(text).replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    if (flat.length() <= limit) {
        return flat;
    }
    return flat.left(limit) + QStringLiteral("…");
}
}

QJsonArray AiTools::schemas()
{
    QJsonArray tools;

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("reason"), property(QStringLiteral("string"), QStringLiteral("Why you need to look at the screen.")));
        tools.append(wrapped(QStringLiteral("take_screenshot"),
                             QStringLiteral("Capture the current remote screen and look at it. Always call this before acting and "
                                            "again after acting to check the result."),
                             properties));
    }

    {
        QJsonObject properties;
        tools.append(wrapped(QStringLiteral("screen_info"),
                             QStringLiteral("Report the remote screen size, the host this session is connected to and whether "
                                            "input can be injected right now."),
                             properties));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("x"), property(QStringLiteral("integer"), QStringLiteral("X coordinate in screenshot pixels.")));
        properties.insert(QStringLiteral("y"), property(QStringLiteral("integer"), QStringLiteral("Y coordinate in screenshot pixels.")));
        tools.append(wrapped(QStringLiteral("mouse_move"),
                             QStringLiteral("Move the remote pointer without clicking, e.g. to reveal a tooltip or hover state."),
                             properties,
                             {QStringLiteral("x"), QStringLiteral("y")}));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("x"), property(QStringLiteral("integer"), QStringLiteral("X coordinate in screenshot pixels.")));
        properties.insert(QStringLiteral("y"), property(QStringLiteral("integer"), QStringLiteral("Y coordinate in screenshot pixels.")));
        properties.insert(QStringLiteral("button"),
                          property(QStringLiteral("string"), QStringLiteral("Which button to click."),
                                   QJsonArray{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("middle")}));
        properties.insert(QStringLiteral("clicks"),
                          property(QStringLiteral("integer"), QStringLiteral("1 for a single click, 2 for a double click.")));
        properties.insert(QStringLiteral("target"),
                          property(QStringLiteral("string"),
                                   QStringLiteral("What you are clicking, in a few words, e.g. \"the OK button\". Shown to the "
                                                  "user for approval.")));
        tools.append(wrapped(QStringLiteral("mouse_click"), QStringLiteral("Click at a point on the remote screen."), properties,
                             {QStringLiteral("x"), QStringLiteral("y")}));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("from_x"), property(QStringLiteral("integer"), QStringLiteral("Start X coordinate.")));
        properties.insert(QStringLiteral("from_y"), property(QStringLiteral("integer"), QStringLiteral("Start Y coordinate.")));
        properties.insert(QStringLiteral("to_x"), property(QStringLiteral("integer"), QStringLiteral("End X coordinate.")));
        properties.insert(QStringLiteral("to_y"), property(QStringLiteral("integer"), QStringLiteral("End Y coordinate.")));
        properties.insert(QStringLiteral("button"),
                          property(QStringLiteral("string"), QStringLiteral("Which button to drag with."),
                                   QJsonArray{QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("middle")}));
        properties.insert(QStringLiteral("target"),
                          property(QStringLiteral("string"),
                                   QStringLiteral("What you are dragging and where, in a few words, e.g. \"the file into the "
                                                  "folder\". Shown to the user for approval.")));
        tools.append(
            wrapped(QStringLiteral("mouse_drag"), QStringLiteral("Press, move and release to drag something on the remote screen."), properties,
                    {QStringLiteral("from_x"), QStringLiteral("from_y"), QStringLiteral("to_x"), QStringLiteral("to_y")}));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("x"), property(QStringLiteral("integer"), QStringLiteral("X coordinate to scroll over.")));
        properties.insert(QStringLiteral("y"), property(QStringLiteral("integer"), QStringLiteral("Y coordinate to scroll over.")));
        properties.insert(QStringLiteral("direction"),
                          property(QStringLiteral("string"), QStringLiteral("Which way to scroll."),
                                   QJsonArray{QStringLiteral("up"), QStringLiteral("down"), QStringLiteral("left"), QStringLiteral("right")}));
        properties.insert(QStringLiteral("notches"), property(QStringLiteral("integer"), QStringLiteral("How many wheel notches, 1 by default.")));
        tools.append(wrapped(QStringLiteral("mouse_scroll"), QStringLiteral("Scroll at a point on the remote screen."), properties,
                             {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("direction")}));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("text"),
                          property(QStringLiteral("string"),
                                   QStringLiteral("The text to type at the current remote focus. Use \\n for Enter.")));
        properties.insert(QStringLiteral("delay_ms"),
                          property(QStringLiteral("integer"),
                                   QStringLiteral("Milliseconds to pause between characters. Use 50 or more for terminals "
                                                  "and search boxes that drop characters.")));
        tools.append(wrapped(QStringLiteral("type_text"),
                             QStringLiteral("Type text into whatever has the keyboard focus on the remote machine. Click the "
                                            "field first so focus is where you expect."),
                             properties,
                             {QStringLiteral("text")}));
    }

    {
        QJsonObject properties;
        properties.insert(
            QStringLiteral("key"),
            property(QStringLiteral("string"),
                     QStringLiteral("Key to press, e.g. return, tab, escape, backspace, delete, space, up, down, left, right, "
                                    "home, end, pageup, pagedown, printscreen, f1…f12, win, menu, or a single character.")));
        properties.insert(QStringLiteral("modifiers"),
                          arrayProperty(QStringLiteral("Modifiers held while pressing the key: control, alt, shift, meta."),
                                        QJsonArray{QStringLiteral("control"), QStringLiteral("alt"), QStringLiteral("shift"),
                                                   QStringLiteral("meta")}));
        tools.append(wrapped(QStringLiteral("press_key"),
                             QStringLiteral("Press a single key, optionally with modifiers held. For a keyboard shortcut such as "
                                            "Ctrl+A or Alt+Tab, prefer the hotkey tool."),
                             properties,
                             {QStringLiteral("key")}));
    }

    {
        QJsonObject properties;
        properties.insert(
            QStringLiteral("keys"),
            property(QStringLiteral("string"),
                     QStringLiteral("The combination as one string, keys joined with \"+\", modifiers first and the final key "
                                    "last: \"ctrl+a\", \"win+d\", \"alt+tab\", \"ctrl+shift+esc\". Modifier names: ctrl, alt, "
                                    "shift, win.")));
        tools.append(wrapped(QStringLiteral("hotkey"),
                             QStringLiteral("Press a keyboard shortcut on the remote machine, e.g. ctrl+a to select all. All "
                                            "keys in the string are held down together, exactly as if you pressed the "
                                            "combination on the keyboard yourself."),
                             properties,
                             {QStringLiteral("keys")}));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("filename"),
                          property(QStringLiteral("string"),
                                   QStringLiteral("File name for the image, without a path, e.g. \"step-1-start-menu\". The "
                                                  "extension is added automatically. Leave empty for a timestamped name.")));
        tools.append(wrapped(QStringLiteral("save_screenshot"),
                             QStringLiteral("Save a full-resolution screenshot of the remote screen as a PNG file on the "
                                            "local computer, in the configured screenshot folder. Use it to document steps "
                                            "for a guide; the reply gives the full path to quote in the write-up."),
                             properties));
    }

    {
        QJsonObject properties;
        properties.insert(QStringLiteral("seconds"), property(QStringLiteral("number"), QStringLiteral("How long to wait.")));
        properties.insert(QStringLiteral("reason"), property(QStringLiteral("string"), QStringLiteral("What you are waiting for.")));
        tools.append(wrapped(QStringLiteral("wait"),
                             QStringLiteral("Pause before the next step, e.g. while the remote machine installs or loads "
                                            "something. Never exceeds 30 seconds."),
                             properties,
                             {QStringLiteral("seconds")}));
    }

    return tools;
}

bool AiTools::isKnownTool(const QString &name)
{
    static const QStringList names = {
        QStringLiteral("take_screenshot"), QStringLiteral("screen_info"),      QStringLiteral("mouse_move"),
        QStringLiteral("mouse_click"),     QStringLiteral("mouse_drag"),       QStringLiteral("mouse_scroll"),
        QStringLiteral("type_text"),       QStringLiteral("press_key"),        QStringLiteral("hotkey"),
        QStringLiteral("save_screenshot"), QStringLiteral("wait"),
    };
    return names.contains(name);
}

AiTools::Risk AiTools::risk(const QString &name, const QJsonObject &arguments)
{
    if (name == QLatin1String("take_screenshot") || name == QLatin1String("screen_info") || name == QLatin1String("wait")) {
        return Risk::Safe;
    }

    if (name == QLatin1String("type_text")) {
        const QString text = arguments.value(QStringLiteral("text")).toString();
        for (const QString &pattern : destructivePatterns()) {
            const QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption);
            if (expression.match(text).hasMatch()) {
                return Risk::Dangerous;
            }
        }
        return Risk::Action;
    }

    if (name == QLatin1String("press_key")) {
        return isDangerousChord(
            normalizedChord(arguments.value(QStringLiteral("key")).toString(), arguments.value(QStringLiteral("modifiers")).toArray()))
            ? Risk::Dangerous
            : Risk::Action;
    }

    if (name == QLatin1String("hotkey")) {
        return isDangerousChord(hotkeyChord(arguments)) ? Risk::Dangerous : Risk::Action;
    }

    return Risk::Action;
}

QString AiTools::describe(const QString &name, const QJsonObject &arguments)
{
    const auto number = [&arguments](const QString &key) {
        return QString::number(int(arguments.value(key).toDouble()));
    };

    if (name == QLatin1String("take_screenshot")) {
        return QStringLiteral("Take a screenshot of the remote screen");
    }
    if (name == QLatin1String("screen_info")) {
        return QStringLiteral("Read the remote screen size");
    }
    if (name == QLatin1String("mouse_move")) {
        return QStringLiteral("Move the remote pointer to (%1, %2)").arg(number(QStringLiteral("x")), number(QStringLiteral("y")));
    }
    if (name == QLatin1String("mouse_click")) {
        const QString button = arguments.value(QStringLiteral("button")).toString();
        const int clicks = int(arguments.value(QStringLiteral("clicks")).toDouble(1));
        const QString target = arguments.value(QStringLiteral("target")).toString().trimmed();
        return QStringLiteral("Click %1 button at (%2, %3)%4%5")
            .arg(button.isEmpty() ? QStringLiteral("left") : button, number(QStringLiteral("x")), number(QStringLiteral("y")),
                 clicks > 1 ? QStringLiteral(" (%1×)").arg(clicks) : QString(),
                 target.isEmpty() ? QString() : QStringLiteral(" — “%1”").arg(truncated(target, 60)));
    }
    if (name == QLatin1String("mouse_drag")) {
        const QString target = arguments.value(QStringLiteral("target")).toString().trimmed();
        return QStringLiteral("Drag from (%1, %2) to (%3, %4)%5")
            .arg(number(QStringLiteral("from_x")), number(QStringLiteral("from_y")), number(QStringLiteral("to_x")), number(QStringLiteral("to_y")),
                 target.isEmpty() ? QString() : QStringLiteral(" — “%1”").arg(truncated(target, 60)));
    }
    if (name == QLatin1String("mouse_scroll")) {
        return QStringLiteral("Scroll %1 at (%2, %3)")
            .arg(arguments.value(QStringLiteral("direction")).toString(), number(QStringLiteral("x")), number(QStringLiteral("y")));
    }
    if (name == QLatin1String("type_text")) {
        return QStringLiteral("Type “%1”").arg(truncated(arguments.value(QStringLiteral("text")).toString()));
    }
    if (name == QLatin1String("press_key")) {
        const QString modifiers = modifierList(arguments.value(QStringLiteral("modifiers")).toArray());
        const QString key = arguments.value(QStringLiteral("key")).toString();
        if (modifiers.isEmpty()) {
            return QStringLiteral("Press %1").arg(key);
        }
        return QStringLiteral("Press %1+%2").arg(modifiers, key);
    }
    if (name == QLatin1String("hotkey")) {
        QStringList chord = hotkeyChord(arguments);
        // Show the natural reading order (ctrl+a), not the sorted match form.
        const QJsonValue keys = arguments.value(QStringLiteral("keys"));
        if (keys.isString()) {
            return QStringLiteral("Press %1").arg(keys.toString());
        }
        return QStringLiteral("Press %1").arg(chord.join(QLatin1Char('+')));
    }
    if (name == QLatin1String("save_screenshot")) {
        const QString filename = arguments.value(QStringLiteral("filename")).toString().trimmed();
        if (filename.isEmpty()) {
            return QStringLiteral("Save a screenshot to a new file on this computer");
        }
        return QStringLiteral("Save a screenshot as “%1” on this computer").arg(truncated(filename, 40));
    }
    if (name == QLatin1String("wait")) {
        return QStringLiteral("Wait %1 s").arg(arguments.value(QStringLiteral("seconds")).toDouble());
    }

    return QStringLiteral("Run %1").arg(name);
}

QString AiTools::dangerReason(const QString &name, const QJsonObject &arguments)
{
    if (name == QLatin1String("type_text")) {
        const QString text = arguments.value(QStringLiteral("text")).toString();
        for (const QString &pattern : destructivePatterns()) {
            const QRegularExpression expression(pattern, QRegularExpression::CaseInsensitiveOption);
            const auto match = expression.match(text);
            if (match.hasMatch()) {
                return QStringLiteral("It contains “%1”, which can destroy data or change the system.").arg(truncated(match.captured(), 40));
            }
        }
    }

    if (name == QLatin1String("press_key")) {
        const QStringList chord =
            normalizedChord(arguments.value(QStringLiteral("key")).toString(), arguments.value(QStringLiteral("modifiers")).toArray());
        for (const DangerousChord &dangerous : dangerousChords()) {
            QStringList expected = dangerous.keys;
            expected.sort();
            if (expected == chord) {
                return QString::fromLatin1(dangerous.reason) + QStringLiteral(".");
            }
        }
    }

    if (name == QLatin1String("hotkey")) {
        const QStringList chord = hotkeyChord(arguments);
        for (const DangerousChord &dangerous : dangerousChords()) {
            QStringList expected = dangerous.keys;
            expected.sort();
            if (expected == chord) {
                return QString::fromLatin1(dangerous.reason) + QStringLiteral(".");
            }
        }
    }

    return QStringLiteral("This changes the remote machine.");
}
