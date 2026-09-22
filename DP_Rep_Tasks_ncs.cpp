// ============================================================================
// DP_Rep_Tasks_ncs.cpp — ncurses interface for DP_Repeated_Tasks
//
// Основа: обычная версия DP_Repeated_Tasks.
// Интерфейс: ncurses, адаптивная разметка, клавиатурная навигация.
//
// Сборка: C++98 + ncurses.
// Пример: g++ -std=c++98 -Wall -Wextra -Wpedantic -O2 -o dp_rep_tasks_ncs DP_Rep_Tasks_ncs.cpp -lncurses
//
// Данные: repeated_tasks.dat. Формат RTASKS3 и все проверки хранения
// полностью совместимы с основной версией.
// ============================================================================

#include <ncurses.h>
#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <clocale>

using namespace std;

// ===================== ОГРАНИЧЕНИЯ =====================
static const int       MAX_DEPTH    = 64;
static const int       MAX_TASKS    = 50000;
static const int       MAX_CHILDREN = 10000;
static const int       MAX_HISTORY  = 5000;
static const long long MIN_TS       = 0LL;
static const long long MAX_TS       = 4102444800LL;

static const char* FILENAME     = "repeated_tasks.dat";
static const char* FILENAME_TMP = "repeated_tasks.dat.tmp";
static const char* FILENAME_BAK = "repeated_tasks.dat.bak";
static const char* MAGIC_V2     = "RTASKS2";
static const char* MAGIC_V3     = "RTASKS3";

// ===================== ПЕРЕЧИСЛЕНИЯ =====================
enum Difficulty { DIFF_HIGH = 0, DIFF_MEDIUM = 1, DIFF_LOW = 2 };
enum Urgency    { URG_HIGH = 0,  URG_MEDIUM = 1,  URG_LOW = 2 };

static string diffToStr(Difficulty d) {
    switch (d) {
        case DIFF_HIGH:   return "Высокая";
        case DIFF_MEDIUM: return "Средняя";
        case DIFF_LOW:    return "Низкая";
    }
    return "?";
}

static string urgToStr(Urgency u) {
    switch (u) {
        case URG_HIGH:   return "Высокая";
        case URG_MEDIUM: return "Средняя";
        case URG_LOW:    return "Низкая";
    }
    return "?";
}

static string levelShort(int v) {
    if (v == 0) return "В";
    if (v == 1) return "С";
    return "Н";
}

static Difficulty toDifficulty(int v) {
    if (v == DIFF_MEDIUM) return DIFF_MEDIUM;
    if (v == DIFF_LOW)    return DIFF_LOW;
    return DIFF_HIGH;
}

static Urgency toUrgency(int v) {
    if (v == URG_MEDIUM) return URG_MEDIUM;
    if (v == URG_LOW)    return URG_LOW;
    return URG_HIGH;
}

// ===================== UTF-8 =====================
// C4Droid предоставляет обычную byte-oriented ncurses.
// Строки хранятся в UTF-8 как есть. После инициализации экрана
// use_legacy_coding(2) запрещает ncurses превращать байты 128..255
// в вид M-XX, поэтому терминал получает исходный UTF-8.
//
// Ширина интерфейса по-прежнему считается вручную через utf8Len():
// для кириллицы и латиницы 1 кодовая точка = 1 экранная ячейка.

static size_t utf8Len(const string& s) {

    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

static string utf8Trunc(const string& s, size_t maxChars);

static string displaySafe(const string& s) {
    string r = s;
    for (size_t i = 0; i < r.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(r[i]);
        if (ch < 32 || ch == 127) r[i] = ' ';
    }
    return r;
}

static void printUtf8At(int y, int x, const string& text) {
    if (y < 0 || x < 0 || y >= LINES || x >= COLS) return;

    string safe = displaySafe(text);
    int remaining = COLS - x;
    if (remaining <= 0) return;

    if (utf8Len(safe) > static_cast<size_t>(remaining))
        safe = utf8Trunc(safe, static_cast<size_t>(remaining));

    mvaddstr(y, x, safe.c_str());
}

static string utf8Trunc(const string& s, size_t maxChars) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == maxChars) return s.substr(0, i);
            ++n;
        }
    }
    return s;
}

static string utf8Tail(const string& s, size_t maxChars) {
    if (utf8Len(s) <= maxChars) return s;
    size_t total = utf8Len(s);
    size_t skip = total - maxChars;
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (n == skip) return s.substr(i);
            ++n;
        }
    }
    return s;
}

static string fitOutput(const string& s, size_t width) {
    if (width == 0) return "";
    if (utf8Len(s) <= width) return s;
    if (width <= 2) return string(width, '.');
    return utf8Trunc(s, width - 2) + "..";
}

static string fitPad(const string& s, size_t width) {
    string r = fitOutput(s, width);
    size_t n = utf8Len(r);
    if (n < width) r.append(width - n, ' ');
    return r;
}

static string centerText(const string& s, size_t width) {
    string r = fitOutput(s, width);
    size_t n = utf8Len(r);
    if (n >= width) return r;
    size_t left = (width - n) / 2;
    return string(left, ' ') + r + string(width - n - left, ' ');
}

static vector<string> wrapText(const string& s, size_t width, size_t maxLines) {
    vector<string> out;
    if (maxLines == 0) return out;
    if (width == 0) {
        out.push_back("");
        return out;
    }

    if (s.empty()) {
        out.push_back("");
        return out;
    }

    size_t pos = 0;
    while (pos < s.size() && out.size() < maxLines) {
        size_t bytes = pos;
        size_t chars = 0;
        while (bytes < s.size() && chars < width) {
            unsigned char c = static_cast<unsigned char>(s[bytes]);
            if ((c & 0xC0) != 0x80) ++chars;

            size_t step = 1;
            if ((c & 0x80) == 0) step = 1;
            else if ((c & 0xE0) == 0xC0) step = 2;
            else if ((c & 0xF0) == 0xE0) step = 3;
            else if ((c & 0xF8) == 0xF0) step = 4;
            if (bytes + step > s.size()) step = 1;
            bytes += step;
        }

        if (bytes == pos) {
            bytes = pos + 1;
        }

        out.push_back(s.substr(pos, bytes - pos));
        pos = bytes;
    }

    if (pos < s.size() && !out.empty()) {
        string tail = out[out.size() - 1];
        if (width > 2)
            out[out.size() - 1] = utf8Trunc(tail, width - 2) + "..";
    }

    return out;
}

// ===================== СТРОКИ / ПАРСИНГ =====================

static string trimStr(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

static bool parseInt(const string& s, int& out) {
    if (s.empty()) return false;

    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = (s[0] == '-');
        i = 1;
        if (s.size() == 1) return false;
    }

    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
        if (v > 2147483647LL) return false;
    }

    out = static_cast<int>(neg ? -v : v);
    return true;
}

static bool parseLL(const string& s, long long& out) {
    if (s.empty()) return false;

    size_t i = 0;
    bool neg = false;
    if (s[0] == '+' || s[0] == '-') {
        neg = (s[0] == '-');
        i = 1;
        if (s.size() == 1) return false;
    }

    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        if (v > (9223372036854775807LL - (s[i] - '0')) / 10) return false;
        v = v * 10 + (s[i] - '0');
    }

    out = neg ? -v : v;
    return true;
}

static string intToStr(long long x) {
    stringstream ss;
    ss << x;
    return ss.str();
}

static string sanitizeField(const string& s) {
    string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] == '\n' || r[i] == '\r') r[i] = ' ';
    return r;
}

// ===================== ВРЕМЯ =====================

static string formatDuration(long long total) {
    if (total < 0) total = 0;

    long long hours  = (total % 86400) / 3600;
    long long days   = total / 86400;
    long long years  = days / 365;
    days -= years * 365;
    long long months = days / 30;
    days -= months * 30;

    if (total < 86400)
        return hours == 0 ? string("<1ч") : intToStr(hours) + "ч";
    if (years == 0 && months == 0)
        return intToStr(days) + "д " + intToStr(hours) + "ч";
    if (years == 0)
        return intToStr(months) + "м " + intToStr(days) + "д " + intToStr(hours) + "ч";

    return intToStr(years) + "г " + intToStr(months) + "м "
         + intToStr(days) + "д " + intToStr(hours) + "ч";
}

static string formatStamp(time_t ts) {
    tm* ti = localtime(&ts);
    if (ti == 0) return "(некорр. дата)";

    char buf[32];
    if (strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", ti) == 0)
        return "(некорр. дата)";

    return string(buf);
}

static bool parseDateString(const string& s, time_t& out) {
    int d, mon, y, h, mn;
    char tail;

    if (sscanf(s.c_str(), "%d.%d.%d %d:%d %c", &d, &mon, &y, &h, &mn, &tail) != 5)
        return false;

    if (y < 1970 || y > 2099 || mon < 1 || mon > 12 ||
        d < 1 || d > 31 || h < 0 || h > 23 || mn < 0 || mn > 59)
        return false;

    tm value;
    memset(&value, 0, sizeof(value));
    value.tm_year = y - 1900;
    value.tm_mon  = mon - 1;
    value.tm_mday = d;
    value.tm_hour = h;
    value.tm_min  = mn;
    value.tm_isdst = -1;

    time_t result = mktime(&value);
    if (result == static_cast<time_t>(-1)) return false;

    tm* check = localtime(&result);
    if (check == 0) return false;

    if (check->tm_year != value.tm_year ||
        check->tm_mon  != value.tm_mon  ||
        check->tm_mday != value.tm_mday ||
        check->tm_hour != value.tm_hour ||
        check->tm_min  != value.tm_min)
        return false;

    out = result;
    return true;
}

// ===================== МОДЕЛЬ ЗАДАЧИ =====================

struct Task {
    int         id;
    string      name;
    string      description;
    vector<time_t> history;
    time_t      dueDate;
    bool        hasDueDate;
    Difficulty  difficulty;
    Urgency     urgency;
    vector<Task> subtasks;
    bool        expanded;

    Task()
        : id(0), dueDate(0), hasDueDate(false),
          difficulty(DIFF_HIGH), urgency(URG_LOW), expanded(true) {}

    bool neverDone() const {
        return history.empty();
    }

    size_t doneCount() const {
        return history.size();
    }

    time_t lastDone() const {
        return history.empty() ? 0 : history[history.size() - 1];
    }

    void addCompletion(time_t when) {
        history.push_back(when);
        sort(history.begin(), history.end());
    }

    bool intervalStats(long long& avg, long long& minI, long long& maxI) const {
        if (history.size() < 2) return false;

        avg = 0;
        minI = maxI = -1;

        for (size_t i = 1; i < history.size(); ++i) {
            long long d = static_cast<long long>(
                difftime(history[i], history[i - 1]));
            if (d < 0) d = 0;

            avg += d;
            if (minI < 0 || d < minI) minI = d;
            if (maxI < 0 || d > maxI) maxI = d;
        }

        avg /= static_cast<long long>(history.size() - 1);
        return true;
    }

    string formatAgo() const {
        if (neverDone()) return "никогда";
        return formatDuration(
            static_cast<long long>(difftime(time(0), lastDone())));
    }

    string formatDue() const {
        if (!hasDueDate) return "-";
        return formatStamp(dueDate);
    }

    string displayName() const {
        return name.empty() ? string("(без названия)") : name;
    }

    int countSubtree() const {
        int n = 1;
        for (size_t i = 0; i < subtasks.size(); ++i)
            n += subtasks[i].countSubtree();
        return n;
    }
};

// ===================== ГЛОБАЛЬНЫЕ ДАННЫЕ =====================

static vector<Task> rootTasks;
static int nextId = 1;
static bool g_dirty = false;
static string g_ioError;

// ===================== ПОИСК =====================

static Task* findById(int id, vector<Task>& tasks) {
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].id == id) return &tasks[i];
        Task* p = findById(id, tasks[i].subtasks);
        if (p) return p;
    }
    return 0;
}

static bool removeById(int id, vector<Task>& tasks) {
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].id == id) {
            tasks.erase(tasks.begin() + i);
            return true;
        }
        if (removeById(id, tasks[i].subtasks)) return true;
    }
    return false;
}

static void collectAll(vector<Task*>& out, vector<Task>& src) {
    for (size_t i = 0; i < src.size(); ++i) {
        out.push_back(&src[i]);
        collectAll(out, src[i].subtasks);
    }
}

static void collectMaxId(const vector<Task>& tasks, int& maxId) {
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].id > maxId) maxId = tasks[i].id;
        collectMaxId(tasks[i].subtasks, maxId);
    }
}

static void reindex(vector<Task>& tasks, vector<int>& seen,
                    int& maxId, bool& renumbered) {
    for (size_t i = 0; i < tasks.size(); ++i) {
        bool dup = (tasks[i].id <= 0);

        for (size_t k = 0; !dup && k < seen.size(); ++k)
            if (seen[k] == tasks[i].id) dup = true;

        if (dup) {
            tasks[i].id = ++maxId;
            renumbered = true;
        } else if (tasks[i].id > maxId) {
            maxId = tasks[i].id;
        }

        seen.push_back(tasks[i].id);
        reindex(tasks[i].subtasks, seen, maxId, renumbered);
    }
}

// ===================== СОХРАНЕНИЕ =====================

static void writeTask(ofstream& out, const Task& t) {
    out << "BEGIN\n"
        << t.id << "\n"
        << sanitizeField(t.name) << "\n"
        << sanitizeField(t.description) << "\n"
        << t.history.size() << "\n";

    for (size_t h = 0; h < t.history.size(); ++h)
        out << static_cast<long long>(t.history[h]) << "\n";

    out << static_cast<long long>(t.dueDate) << "\n"
        << (t.hasDueDate ? 1 : 0) << "\n"
        << static_cast<int>(t.difficulty) << "\n"
        << static_cast<int>(t.urgency) << "\n"
        << (t.expanded ? 1 : 0) << "\n"
        << t.subtasks.size() << "\n";

    for (size_t i = 0; i < t.subtasks.size(); ++i)
        writeTask(out, t.subtasks[i]);

    out << "END\n";
}

static bool writeAllTo(const char* path) {
    g_ioError.clear();

    ofstream out(path);
    if (!out) {
        g_ioError = "Не удалось открыть файл для записи.";
        return false;
    }

    out << MAGIC_V3 << "\n" << nextId << "\n" << rootTasks.size() << "\n";
    for (size_t i = 0; i < rootTasks.size(); ++i)
        writeTask(out, rootTasks[i]);

    out.flush();
    if (!out.good()) {
        g_ioError = "Ошибка записи. Возможно, недостаточно места.";
        out.close();
        return false;
    }

    out.close();
    if (out.fail()) {
        g_ioError = "Ошибка закрытия файла данных.";
        return false;
    }

    return true;
}

static bool saveData() {
    string err;

    if (!writeAllTo(FILENAME_TMP)) {
        remove(FILENAME_TMP);
        return false;
    }

    remove(FILENAME_BAK);
    rename(FILENAME, FILENAME_BAK);

    if (rename(FILENAME_TMP, FILENAME) != 0) {
        g_ioError = "Не удалось заменить файл данных. Предыдущая версия сохранена в "
                    + string(FILENAME_BAK) + ".";
        return false;
    }

    g_dirty = false;
    g_ioError.clear();
    return true;
}

// ===================== ЗАГРУЗКА =====================

struct Loader {
    ifstream& in;
    int ver;
    int total;
    string err;

    Loader(ifstream& stream, int version)
        : in(stream), ver(version), total(0) {}

    bool line(string& out) {
        if (!getline(in, out)) {
            err = "Файл оборван.";
            return false;
        }

        if (!out.empty() && out[out.size() - 1] == '\r')
            out.erase(out.size() - 1);

        return true;
    }

    bool intLine(int& out, int lo, int hi, const char* what) {
        string s;
        if (!line(s)) return false;

        if (!parseInt(trimStr(s), out)) {
            err = string("Нечисловое поле: ") + what;
            return false;
        }

        if (out < lo || out > hi) {
            err = string("Значение вне диапазона: ") + what;
            return false;
        }

        return true;
    }

    bool timeLine(time_t& out) {
        string s;
        long long v;

        if (!line(s)) return false;
        if (!parseLL(trimStr(s), v)) {
            err = "Нечисловая метка времени.";
            return false;
        }

        if (v < MIN_TS) v = MIN_TS;
        if (v > MAX_TS) v = MAX_TS;

        out = static_cast<time_t>(v);
        return true;
    }

    bool readTask(Task& t, int depth) {
        if (depth > MAX_DEPTH) {
            err = "Слишком глубокая вложенность.";
            return false;
        }

        if (++total > MAX_TASKS) {
            err = "Слишком много задач в файле.";
            return false;
        }

        string s;
        if (!line(s)) return false;

        if (trimStr(s) != "BEGIN") {
            err = "Ожидался маркер BEGIN.";
            return false;
        }

        int v;

        if (!intLine(t.id, 0, 2147483647, "id")) return false;
        if (!line(t.name)) return false;
        if (!line(t.description)) return false;

        t.history.clear();

        if (ver >= 3) {
            int hcnt;
            if (!intLine(hcnt, 0, MAX_HISTORY, "число отметок о выполнении"))
                return false;

            t.history.reserve(static_cast<size_t>(hcnt));

            for (int h = 0; h < hcnt; ++h) {
                time_t ts;
                if (!timeLine(ts)) return false;
                t.history.push_back(ts);
            }

            sort(t.history.begin(), t.history.end());
        } else {
            time_t last;
            if (!timeLine(last)) return false;

            bool never = false;
            if (ver == 2) {
                if (!intLine(v, 0, 1, "neverDone")) return false;
                never = (v != 0);
            }

            if (!never) t.history.push_back(last);
        }

        if (!timeLine(t.dueDate)) return false;
        if (!intLine(v, 0, 1, "hasDueDate")) return false;
        t.hasDueDate = (v != 0);

        if (!intLine(v, -2147483647, 2147483647, "difficulty"))
            return false;
        t.difficulty = toDifficulty(v);

        if (!intLine(v, -2147483647, 2147483647, "urgency"))
            return false;
        t.urgency = toUrgency(v);

        if (ver >= 2) {
            if (!intLine(v, 0, 1, "expanded")) return false;
            t.expanded = (v != 0);
        } else {
            t.expanded = true;
        }

        int cnt;
        if (!intLine(cnt, 0, MAX_CHILDREN, "число подзадач")) return false;

        t.subtasks.clear();
        t.subtasks.reserve(static_cast<size_t>(cnt));

        for (int i = 0; i < cnt; ++i) {
            Task child;
            if (!readTask(child, depth + 1)) return false;
            t.subtasks.push_back(child);
        }

        if (!line(s)) return false;

        if (trimStr(s) != "END") {
            err = "Ожидался маркер END.";
            return false;
        }

        return true;
    }
};

static bool loadData() {
    g_ioError.clear();

    ifstream in(FILENAME);
    if (!in) return true;

    string first;
    if (!getline(in, first)) {
        g_ioError = "Файл данных пуст или повреждён.";
        return false;
    }

    if (!first.empty() && first[first.size() - 1] == '\r')
        first.erase(first.size() - 1);

    string magic = trimStr(first);
    int ver = (magic == MAGIC_V3) ? 3 : (magic == MAGIC_V2 ? 2 : 1);

    int loadedNextId = 1;

    if (ver >= 2) {
        string s;
        if (!getline(in, s) || !parseInt(trimStr(s), loadedNextId))
            loadedNextId = 1;
    } else {
        if (!parseInt(trimStr(first), loadedNextId))
            loadedNextId = 1;
    }

    string s;
    int cnt = 0;

    if (!getline(in, s) ||
        !parseInt(trimStr(s), cnt) ||
        cnt < 0 || cnt > MAX_TASKS) {
        g_ioError = "Повреждён файл данных: неверное число корневых задач.";
        return false;
    }

    Loader loader(in, ver);
    vector<Task> loaded;
    loaded.reserve(static_cast<size_t>(cnt));

    for (int i = 0; i < cnt; ++i) {
        Task t;
        if (!loader.readTask(t, 1)) {
            g_ioError = "Повреждён файл данных: " + loader.err;
            return false;
        }
        loaded.push_back(t);
    }

    rootTasks.swap(loaded);

    int maxId = 0;
    collectMaxId(rootTasks, maxId);

    vector<int> seen;
    bool renumbered = false;
    reindex(rootTasks, seen, maxId, renumbered);

    nextId = (loadedNextId > maxId ? loadedNextId : maxId + 1);
    if (nextId < 1) nextId = 1;

    g_dirty = renumbered;

    if (renumbered)
        g_ioError = "В файле были повторяющиеся ID. Они перенумерованы.";

    return true;
}

// ===================== NCURSES UI =====================

enum Screen {
    SCR_MENU = 0,
    SCR_LIST,
    SCR_DETAIL,
    SCR_FORM,
    SCR_KANBAN,
    SCR_HISTORY,
    SCR_HISTORY_ADD,
    SCR_CONFIRM,
    SCR_MESSAGE,
    SCR_TOO_SMALL
};

enum ConfirmKind {
    CONF_NONE = 0,
    CONF_DELETE_LIST,
    CONF_DELETE_DETAIL,
    CONF_DELETE_HISTORY,
    CONF_RELOAD,
    CONF_EXIT
};

class Application {
    int screen;
    int rows;
    int cols;

    int menuSel;

    vector< pair<int,int> > flat;
    int listSel;
    int listScroll;

    int detailTaskId;

    bool formEdit;
    int formParentId;
    int formField;
    int formFieldCount;
    string fName;
    string fDesc;
    string fLast;
    string fDue;
    bool fHasLast;
    bool fHasDue;
    Difficulty fDiff;
    Urgency fUrg;

    int historyTaskId;
    int historySel;
    int historyScroll;
    string dateInput;

    int kanbanMode;
    int kanbanCol;
    int kanbanRow[3];
    vector<Task*> kanbanItems[3];

    ConfirmKind confirmKind;
    int confirmTaskId;

    string messageText;
    int messageReturnScreen;

    int messageReturnIndex;

    static const int CP_HEADER    = 1;
    static const int CP_NORMAL    = 2;
    static const int CP_SELECTED  = 3;
    static const int CP_SECONDARY = 4;
    static const int CP_DANGER    = 5;
    static const int CP_BORDER    = 6;

public:
    Application()
        : screen(SCR_MENU), rows(0), cols(0),
          menuSel(0), listSel(0), listScroll(0),
          detailTaskId(-1),
          formEdit(false), formParentId(-1), formField(0),
          formFieldCount(0), fHasLast(false), fHasDue(false),
          fDiff(DIFF_HIGH), fUrg(URG_LOW),
          historyTaskId(-1), historySel(0), historyScroll(0),
          kanbanMode(0), kanbanCol(0),
          confirmKind(CONF_NONE), confirmTaskId(-1),
          messageReturnScreen(SCR_MENU), messageReturnIndex(0) {
        kanbanRow[0] = kanbanRow[1] = kanbanRow[2] = 0;
    }

    int run() {
        setlocale(LC_ALL, "");

        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);

#ifdef NCURSES_VERSION
        use_legacy_coding(2);
#endif

        if (has_colors()) {
            start_color();
            init_pair(CP_HEADER,    COLOR_CYAN,    COLOR_BLACK);
            init_pair(CP_NORMAL,    COLOR_WHITE,   COLOR_BLACK);
            init_pair(CP_SELECTED,  COLOR_BLACK,   COLOR_WHITE);
            init_pair(CP_SECONDARY, COLOR_MAGENTA, COLOR_BLACK);
            init_pair(CP_DANGER,    COLOR_RED,     COLOR_BLACK);
            init_pair(CP_BORDER,    COLOR_BLUE,    COLOR_BLACK);
        }

        if (!loadData()) {
            messageText = g_ioError;
            messageReturnScreen = SCR_MENU;
            screen = SCR_MESSAGE;
        } else if (!g_ioError.empty()) {
            messageText = g_ioError;
            g_ioError.clear();
            messageReturnScreen = SCR_MENU;
            screen = SCR_MESSAGE;
        }

        while (screen != -1) {
            getmaxyx(stdscr, rows, cols);
            clear();

            if (rows < 15 || cols < 50) {
                drawTooSmall();
                refresh();
                getch();
                continue;
            }

            switch (screen) {
                case SCR_MENU:        drawMenu();        break;
                case SCR_LIST:        drawTaskList();    break;
                case SCR_DETAIL:      drawDetail();      break;
                case SCR_FORM:        drawForm();        break;
                case SCR_KANBAN:      drawKanban();      break;
                case SCR_HISTORY:     drawHistory();     break;
                case SCR_HISTORY_ADD: drawHistoryAdd();  break;
                case SCR_CONFIRM:     drawConfirm();     break;
                case SCR_MESSAGE:     drawMessage();     break;
                default: break;
            }

            if (screen != SCR_TOO_SMALL)
                drawStatusBar();

            refresh();

            int ch = getch();
            switch (screen) {
                case SCR_MENU:        handleMenu(ch);        break;
                case SCR_LIST:        handleList(ch);        break;
                case SCR_DETAIL:      handleDetail(ch);      break;
                case SCR_FORM:        handleForm(ch);        break;
                case SCR_KANBAN:      handleKanban(ch);      break;
                case SCR_HISTORY:     handleHistory(ch);     break;
                case SCR_HISTORY_ADD: handleHistoryAdd(ch);  break;
                case SCR_CONFIRM:     handleConfirm(ch);     break;
                case SCR_MESSAGE:     handleMessage(ch);     break;
                default: break;
            }
        }

        endwin();
        return 0;
    }

private:
    void useAttr(int pair, bool bold) {
        if (has_colors()) {
            attron(COLOR_PAIR(pair));
            if (bold) attron(A_BOLD);
        } else if (bold) {
            attron(A_BOLD);
        }
    }

    void offAttr(int pair, bool bold) {
        if (bold) attroff(A_BOLD);
        if (has_colors()) attroff(COLOR_PAIR(pair));
    }

    void drawBox(int y, int x, int h, int w) {
        if (h < 2 || w < 2) return;

        useAttr(CP_BORDER, false);

        mvhline(y, x + 1, ACS_HLINE, w - 2);
        mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
        mvvline(y + 1, x, ACS_VLINE, h - 2);
        mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);

        mvaddch(y, x, ACS_ULCORNER);
        mvaddch(y, x + w - 1, ACS_URCORNER);
        mvaddch(y + h - 1, x, ACS_LLCORNER);
        mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);

        offAttr(CP_BORDER, false);
    }

    void drawTitleLine(const string& title) {
        useAttr(CP_HEADER, true);

        string t = " " + title + " ";
        int x = (cols - static_cast<int>(utf8Len(t))) / 2;
        if (x < 0) x = 0;

        printUtf8At(0, x, fitOutput(t, cols));

        offAttr(CP_HEADER, true);
    }

    void drawStatusBar() {
        if (rows < 2) return;

        string status;

        switch (screen) {
            case SCR_MENU:
                status = "[↑↓] Выбор  [Enter] Открыть  [S] Сохр.  [R] Обновить  [Q] Выход";
                break;
            case SCR_LIST:
                status = "[B] Меню  [↑↓] Выбор  [Enter] Детали  [+/-] Ветка  [A] Доб.  [E] Ред.  [D] Уд.  [X] Вып.";
                break;
            case SCR_DETAIL:
                status = "[B] Назад  [1] Вып.  [2] Под.  [3] Ред.  [4] Уд.  [5] Ист.";
                break;
            case SCR_FORM:
                status = "[B] Отмена  [Tab/↑↓] Поле  [←→] Знач.  [Enter] Сохр.";
                break;
            case SCR_KANBAN:
                status = "[B] Меню  [←→] Кол.  [↑↓] Задача  [Enter] Детали  [Tab] Режим";
                break;
            case SCR_HISTORY:
                status = "[B] Назад  [↑↓] Выбор  [A] Доб.  [D] Уд.";
                break;
            case SCR_HISTORY_ADD:
                status = "[B] Отмена  [Enter] Сохр.";
                break;
            case SCR_CONFIRM:
                status = "[B] Отмена  [←→] Выбор  [Enter] Подтвердить";
                break;
            case SCR_MESSAGE:
                status = "[B] Назад  [Enter/Space] OK";
                break;
            default:
                status = "";
                break;
        }

        useAttr(CP_HEADER, false);
        attron(A_REVERSE);

        string line = fitPad(status, static_cast<size_t>(cols));
        printUtf8At(rows - 1, 0, line);

        attroff(A_REVERSE);
        offAttr(CP_HEADER, false);
    }

    void drawTooSmall() {
        string title = "Терминал слишком мал";
        string msg1 = "Минимальный размер: 50x15";
        string msg2 = intToStr(cols) + "x" + intToStr(rows);

        useAttr(CP_HEADER, true);
        printUtf8At(1, max(0, (cols - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        useAttr(CP_NORMAL, false);
        printUtf8At(3, max(0, (cols - static_cast<int>(utf8Len(msg1))) / 2), msg1);
        printUtf8At(4, max(0, (cols - static_cast<int>(utf8Len(msg2))) / 2), msg2);
        printUtf8At(6, 0, fitPad("Увеличьте окно терминала и продолжите.", static_cast<size_t>(cols)));
        offAttr(CP_NORMAL, false);
    }

    void rebuildFlat() {
        flat.clear();
        collectFlatVisible(flat, rootTasks, 0);
        if (flat.empty()) {
            listSel = 0;
            listScroll = 0;
        } else {
            if (listSel >= static_cast<int>(flat.size()))
                listSel = static_cast<int>(flat.size()) - 1;
            if (listSel < 0) listSel = 0;
            adjustListScroll();
        }
    }

    void collectFlatVisible(vector< pair<int,int> >& out,
                            vector<Task>& tasks, int level) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            out.push_back(make_pair(level, tasks[i].id));
            if (tasks[i].expanded)
                collectFlatVisible(out, tasks[i].subtasks, level + 1);
        }
    }

    void adjustListScroll() {
        int visible = rows - 3;
        if (visible < 1) visible = 1;

        if (listSel < listScroll)
            listScroll = listSel;

        if (listSel >= listScroll + visible)
            listScroll = listSel - visible + 1;

        if (listScroll < 0) listScroll = 0;

        int maxScroll = static_cast<int>(flat.size()) - visible;
        if (maxScroll < 0) maxScroll = 0;

        if (listScroll > maxScroll)
            listScroll = maxScroll;
    }

    int numberWidth() const {
        int maxNumber = static_cast<int>(flat.size());
        int width = 1;
        while (maxNumber >= 10) {
            ++width;
            maxNumber /= 10;
        }
        return width;
    }

    string listPrefix(int index) {
        Task* t = findById(flat[index].second, rootTasks);
        if (!t) return "";

        int level = flat[index].first;
        string marker;
        if (t->subtasks.empty())
            marker = "[ ] ";
        else
            marker = t->expanded ? "[-] " : "[+] ";

        string n = intToStr(index + 1);
        return string(level * 2, ' ') + marker + fitPad(n + ". ", numberWidth() + 2);
    }

    void drawListHeaders() {
        int width = cols;
        int ageW = 14;
        int prioW = 13;
        int dueW = 17;
        int gap = 1;

        bool showPrio = width >= 70;
        bool showDue  = width >= 96;

        int fixed = ageW + gap;
        if (showPrio) fixed += prioW + gap;
        if (showDue)  fixed += dueW + gap;

        int infoX = cols - fixed;
        if (infoX < 1) infoX = 1;

        useAttr(CP_SECONDARY, true);
        int taskHeaderX = min(max(12, infoX - 1), max(0, cols - fixed - 1));
        printUtf8At(1, taskHeaderX, fitOutput("Задача",
                     static_cast<size_t>(max(1, infoX - taskHeaderX - 1))));

        int x = infoX;
        printUtf8At(1, x, fitPad("Прошло", ageW));
        x += ageW + gap;

        if (showPrio) {
            printUtf8At(1, x, fitPad("С/Ср", prioW));
            x += prioW + gap;
        }

        if (showDue)
            printUtf8At(1, x, fitPad("Не позже", dueW));

        offAttr(CP_SECONDARY, true);
        mvhline(2, 0, ACS_HLINE, cols);
    }

    void drawTaskList() {
        rebuildFlat();
        drawTitleLine("СПИСОК ЗАДАЧ");

        string count = "Задач: " + intToStr(static_cast<long long>(flat.size()));
        if (g_dirty) count += " *";
        useAttr(CP_SECONDARY, false);
        printUtf8At(1, 0, fitOutput(count,
                     static_cast<size_t>(max(1, cols - 1))));
        offAttr(CP_SECONDARY, false);
        drawListHeaders();

        int visible = rows - 4;
        if (visible < 1) visible = 1;

        int ageW = 14;
        int prioW = 13;
        int dueW = 17;
        int gap = 1;

        bool showPrio = cols >= 70;
        bool showDue  = cols >= 96;

        int fixed = ageW + gap;
        if (showPrio) fixed += prioW + gap;
        if (showDue)  fixed += dueW + gap;

        int infoX = cols - fixed;
        if (infoX < 1) infoX = 1;

        int nWidth = numberWidth();

        for (int row = 0; row < visible; ++row) {
            int idx = listScroll + row;
            if (idx >= static_cast<int>(flat.size())) break;

            int y = 3 + row;
            Task* t = findById(flat[idx].second, rootTasks);
            if (!t) continue;

            bool selected = (idx == listSel);
            if (selected) useAttr(CP_SELECTED, true);
            else useAttr(CP_NORMAL, false);

            mvhline(y, 0, ' ', cols);

            string prefix = listPrefix(idx);
            int nameX = static_cast<int>(utf8Len(prefix));

            string name = t->displayName();
            int nameW = infoX - nameX - 1;
            if (nameW < 1) nameW = 1;

            printUtf8At(y, nameX, fitPad(name, static_cast<size_t>(nameW)));

            int x = infoX;
            string age = "[" + t->formatAgo() + "]";
            printUtf8At(y, x, fitPad(age, ageW));
            x += ageW + gap;

            if (showPrio) {
                string prio = "С:" + levelShort(static_cast<int>(t->difficulty))
                            + " Ср:" + levelShort(static_cast<int>(t->urgency));
                printUtf8At(y, x, fitPad(prio, prioW));
                x += prioW + gap;
            }

            if (showDue) {
                string due = t->hasDueDate ? t->formatDue() : "-";
                printUtf8At(y, x, fitPad(due, dueW));
            }

            if (selected) offAttr(CP_SELECTED, true);
            else offAttr(CP_NORMAL, false);
        }
    }

    void drawMenu() {
        drawTitleLine("Repeated Tasks");

        const char* items[7] = {
            "Список задач",
            "Добавить корневую задачу",
            "Канбан: Сложность",
            "Канбан: Срочность",
            "Сохранить на диск",
            "Перезагрузить с диска",
            "Выход (с сохранением)"
        };

        int boxW = min(60, cols - 4);
        if (boxW < 40) boxW = 40;

        int boxH = 11;
        int y = max(2, (rows - boxH) / 2);
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        for (int i = 0; i < 7; ++i) {
            int lineY = y + 2 + i;
            string line = intToStr(i == 6 ? 0 : i + 1) + ". " + items[i];

            if (i == menuSel) {
                useAttr(CP_SELECTED, true);
                printUtf8At(lineY, x + 3, fitPad(line, static_cast<size_t>(boxW - 6)));
                offAttr(CP_SELECTED, true);
            } else {
                useAttr(CP_NORMAL, false);
                printUtf8At(lineY, x + 3, fitPad(line, static_cast<size_t>(boxW - 6)));
                offAttr(CP_NORMAL, false);
            }
        }

        useAttr(CP_SECONDARY, false);
        string info = "Корневых задач: " + intToStr(rootTasks.size());
        if (g_dirty) info += "   есть несохранённые изменения";
        printUtf8At(min(rows - 2, y + boxH + 1), x, fitOutput(info, static_cast<size_t>(boxW)));
        offAttr(CP_SECONDARY, false);
    }

    void drawDetail() {
        Task* t = findById(detailTaskId, rootTasks);
        if (!t) {
            screen = SCR_LIST;
            return;
        }

        vector<string> lines;
        lines.push_back("Название: " + t->displayName());

        vector<string> desc = wrapText(
            t->description.empty() ? string("-") : t->description,
            48, 3);

        for (size_t i = 0; i < desc.size(); ++i)
            lines.push_back("Описание: " + desc[i]);

        lines.push_back("Послед.выполн: " +
                        t->formatAgo() +
                        (t->neverDone() ? "" : " назад"));

        string done = "Выполнено раз: " +
                      intToStr(static_cast<long long>(t->doneCount()));
        long long avg, mn, mx;
        if (t->intervalStats(avg, mn, mx))
            done += "   (обычно раз в " + formatDuration(avg) + ")";
        lines.push_back(done);

        lines.push_back("Не позже: " + t->formatDue());
        lines.push_back("Сложность: " + diffToStr(t->difficulty));
        lines.push_back("Срочность: " + urgToStr(t->urgency));
        lines.push_back("Подзадач: " +
                        intToStr(static_cast<long long>(t->subtasks.size())));

        int contentH = static_cast<int>(lines.size()) + 3;
        int boxH = min(rows - 3, max(13, contentH));
        int boxW = min(cols - 4, 72);
        if (boxW < 44) boxW = 44;

        int y = max(1, (rows - boxH) / 2);
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        useAttr(CP_HEADER, true);
        string title = " ЗАДАЧА #" + intToStr(t->id) + " ";
        printUtf8At(y, x + max(2, (boxW - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        int lineY = y + 2;
        for (size_t i = 0; i < lines.size() && lineY < y + boxH - 3; ++i) {
            string label = lines[i];

            size_t colon = label.find(':');
            if (colon != string::npos && colon < 18) {
                string left = label.substr(0, colon + 1);
                string right = label.substr(colon + 1);
                useAttr(CP_SECONDARY, true);
                printUtf8At(lineY, x + 2, fitPad(left, 17));
                offAttr(CP_SECONDARY, true);

                useAttr(CP_NORMAL, false);
                printUtf8At(lineY, x + 20, fitOutput(trimStr(right), static_cast<size_t>(boxW - 23)));
                offAttr(CP_NORMAL, false);
            } else {
                useAttr(CP_NORMAL, false);
                printUtf8At(lineY, x + 2, fitOutput(label, static_cast<size_t>(boxW - 4)));
                offAttr(CP_NORMAL, false);
            }
            ++lineY;
        }

        useAttr(CP_SECONDARY, false);
        string actions = "[1] Выполнена   [2] Подзадача   [3] Ред.   [4] Удалить   [5] История";
        printUtf8At(y + boxH - 2, x + 2, fitOutput(actions, static_cast<size_t>(boxW - 4)));
        offAttr(CP_SECONDARY, false);
    }

    int formHeight() const {
        return formFieldCount + 5;
    }

    string formLabel(int index) const {
        if (!formEdit) {
            switch (index) {
                case 0: return "Название:";
                case 1: return "Описание:";
                case 2: return "Выполнялась:";
                case 3: return "Послед.выполн:";
                case 4: return "Есть \"не позже\":";
                case 5: return "Дата срока:";
                case 6: return "Сложность:";
                case 7: return "Срочность:";
            }
        } else {
            switch (index) {
                case 0: return "Название:";
                case 1: return "Описание:";
                case 2: return "Есть \"не позже\":";
                case 3: return "Дата срока:";
                case 4: return "Сложность:";
                case 5: return "Срочность:";
            }
        }
        return "";
    }

    string formValue(int index) const {
        if (!formEdit) {
            switch (index) {
                case 0: return fName;
                case 1: return fDesc;
                case 2: return fHasLast ? "Да" : "Нет";
                case 3: return fLast;
                case 4: return fHasDue ? "Да" : "Нет";
                case 5: return fDue;
                case 6: return diffToStr(fDiff);
                case 7: return urgToStr(fUrg);
            }
        } else {
            switch (index) {
                case 0: return fName;
                case 1: return fDesc;
                case 2: return fHasDue ? "Да" : "Нет";
                case 3: return fDue;
                case 4: return diffToStr(fDiff);
                case 5: return urgToStr(fUrg);
            }
        }
        return "";
    }

    bool isFormTextField(int index) const {
        if (!formEdit)
            return index == 0 || index == 1 || index == 3 || index == 5;
        return index == 0 || index == 1 || index == 3;
    }

    void toggleFormValue(int index, int direction) {
        if (!formEdit) {
            if (index == 2) fHasLast = !fHasLast;
            else if (index == 4) fHasDue = !fHasDue;
            else if (index == 6) {
                int v = static_cast<int>(fDiff);
                v = (v + (direction > 0 ? 1 : 2)) % 3;
                fDiff = toDifficulty(v);
            } else if (index == 7) {
                int v = static_cast<int>(fUrg);
                v = (v + (direction > 0 ? 1 : 2)) % 3;
                fUrg = toUrgency(v);
            }
        } else {
            if (index == 2) fHasDue = !fHasDue;
            else if (index == 4) {
                int v = static_cast<int>(fDiff);
                v = (v + (direction > 0 ? 1 : 2)) % 3;
                fDiff = toDifficulty(v);
            } else if (index == 5) {
                int v = static_cast<int>(fUrg);
                v = (v + (direction > 0 ? 1 : 2)) % 3;
                fUrg = toUrgency(v);
            }
        }
    }

    string& formTextRef(int index) {
        if (!formEdit) {
            if (index == 0) return fName;
            if (index == 1) return fDesc;
            if (index == 3) return fLast;
            return fDue;
        }

        if (index == 0) return fName;
        if (index == 1) return fDesc;
        return fDue;
    }

    void drawForm() {
        int boxH = formHeight();
        if (boxH > rows - 2) boxH = rows - 2;

        int boxW = min(cols - 4, 78);
        if (boxW < 46) boxW = 46;

        int y = max(1, (rows - boxH) / 2);
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        useAttr(CP_HEADER, true);
        string title = formEdit ? " РЕДАКТИРОВАНИЕ " : " НОВАЯ ЗАДАЧА ";
        printUtf8At(y, x + max(2, (boxW - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        int labelW = 19;

        for (int i = 0; i < formFieldCount; ++i) {
            int lineY = y + 2 + i;
            if (lineY >= y + boxH - 2) break;

            bool active = (i == formField);

            if (active) useAttr(CP_SELECTED, true);
            else useAttr(CP_SECONDARY, true);

            string label = fitPad(formLabel(i), static_cast<size_t>(labelW));
            printUtf8At(lineY, x + 2, label);

            if (active) offAttr(CP_SELECTED, true);
            else offAttr(CP_SECONDARY, true);

            string value = formValue(i);
            int valueX = x + 2 + labelW;
            int valueW = boxW - labelW - 5;
            if (valueW < 8) valueW = 8;

            string display = isFormTextField(i) ? utf8Tail(value, static_cast<size_t>(valueW))
                                                : fitOutput(value, static_cast<size_t>(valueW));

            if (active) useAttr(CP_SELECTED, false);
            else useAttr(CP_NORMAL, false);

            printUtf8At(lineY, valueX, fitPad(display, static_cast<size_t>(valueW)));

            if (active)
                move(lineY, valueX + static_cast<int>(utf8Len(display)));

            if (active) offAttr(CP_SELECTED, false);
            else offAttr(CP_NORMAL, false);
        }
    }

    void drawHistory() {
        Task* t = findById(historyTaskId, rootTasks);
        if (!t) {
            screen = SCR_LIST;
            return;
        }

        drawTitleLine("ИСТОРИЯ ВЫПОЛНЕНИЙ");

        useAttr(CP_SECONDARY, false);
        printUtf8At(1, 2, fitOutput("Задача: " + t->displayName(),
                           static_cast<size_t>(max(1, cols - 4))));
        offAttr(CP_SECONDARY, false);

        if (t->history.empty()) {
            useAttr(CP_NORMAL, false);
            printUtf8At(3, 2, fitOutput(
                "История пуста. Клавиша A добавит прошлое выполнение.",
                static_cast<size_t>(max(1, cols - 4))));
            offAttr(CP_NORMAL, false);
            return;
        }

        long long avg, mn, mx;
        string summary = "Всего: " +
            intToStr(static_cast<long long>(t->doneCount()));
        if (t->intervalStats(avg, mn, mx))
            summary += "   Обычно: " + formatDuration(avg)
                     + "   От " + formatDuration(mn)
                     + " до " + formatDuration(mx);

        useAttr(CP_SECONDARY, false);
        printUtf8At(2, 2, fitOutput(summary,
                  static_cast<size_t>(max(1, cols - 4))));
        offAttr(CP_SECONDARY, false);

        int numW = 4;
        int dateW = 16;
        int gap = 2;
        int gapW = cols - 4 - numW - dateW - gap * 2;
        if (gapW < 8) gapW = 8;

        int visible = rows - 7;
        if (visible < 1) visible = 1;

        if (historySel >= static_cast<int>(t->history.size()))
            historySel = static_cast<int>(t->history.size()) - 1;
        if (historySel < 0) historySel = 0;

        if (historySel < historyScroll)
            historyScroll = historySel;
        if (historySel >= historyScroll + visible)
            historyScroll = historySel - visible + 1;

        int maxScroll = static_cast<int>(t->history.size()) - visible;
        if (maxScroll < 0) maxScroll = 0;
        if (historyScroll > maxScroll) historyScroll = maxScroll;

        useAttr(CP_SECONDARY, true);
        printUtf8At(4, 2, fitPad("№", numW));
        printUtf8At(4, 2 + numW + gap, fitPad("Когда", dateW));
        printUtf8At(4, 2 + numW + gap + dateW + gap, fitPad("Прошло с предыдущего", static_cast<size_t>(gapW)));
        offAttr(CP_SECONDARY, true);

        mvhline(5, 2, ACS_HLINE, max(1, cols - 4));

        for (int row = 0; row < visible; ++row) {
            int idx = historyScroll + row;
            if (idx >= static_cast<int>(t->history.size())) break;

            int y = 6 + row;
            bool selected = (idx == historySel);

            if (selected) useAttr(CP_SELECTED, true);
            else useAttr(CP_NORMAL, false);

            mvhline(y, 0, ' ', cols);

            string gapText = "-";
            if (idx > 0)
                gapText = formatDuration(
                    static_cast<long long>(
                        difftime(t->history[idx], t->history[idx - 1])));

            printUtf8At(y, 2, fitPad(intToStr(static_cast<long long>(idx + 1)),
                            numW));
            printUtf8At(y, 2 + numW + gap, fitPad(formatStamp(t->history[idx]),
                            dateW));
            printUtf8At(y, 2 + numW + gap + dateW + gap, fitPad(gapText, static_cast<size_t>(gapW)));

            if (selected) offAttr(CP_SELECTED, true);
            else offAttr(CP_NORMAL, false);
        }
    }

    void drawHistoryAdd() {
        int boxW = min(cols - 4, 62);
        if (boxW < 42) boxW = 42;
        int boxH = 7;

        int y = (rows - boxH) / 2;
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        useAttr(CP_HEADER, true);
        string title = " ДОБАВИТЬ ВЫПОЛНЕНИЕ ";
        printUtf8At(y, x + max(2, (boxW - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        useAttr(CP_SECONDARY, true);
        printUtf8At(y + 2, x + 2, fitPad(
            "Дата (ДД.ММ.ГГГГ ЧЧ:ММ):",
            static_cast<size_t>(boxW - 4)));
        offAttr(CP_SECONDARY, true);

        useAttr(CP_SELECTED, false);
        string display = utf8Tail(dateInput, static_cast<size_t>(boxW - 8));
        printUtf8At(y + 3, x + 3, fitPad(display, static_cast<size_t>(boxW - 6)));
        move(y + 3, x + 3 + static_cast<int>(utf8Len(display)));
        offAttr(CP_SELECTED, false);
    }

    void drawKanban() {
        string title = kanbanMode == 0
            ? "КАНБАН: СЛОЖНОСТЬ"
            : "КАНБАН: СРОЧНОСТЬ";
        drawTitleLine(title);

        int startY = 2;
        int bodyH = rows - 4;
        if (bodyH < 3) bodyH = 3;

        int colW = (cols - 4) / 3;
        if (colW < 10) colW = 10;

        const char* headers[3] = { "ВЫСОКАЯ", "СРЕДНЯЯ", "НИЗКАЯ" };

        rebuildKanban();

        for (int c = 0; c < 3; ++c) {
            int x = 1 + c * (colW + 1);

            useAttr(CP_HEADER, true);
            string head = centerText(headers[c], static_cast<size_t>(colW));
            if (c == kanbanCol) attron(A_REVERSE);
            printUtf8At(startY, x, head);
            if (c == kanbanCol) attroff(A_REVERSE);
            offAttr(CP_HEADER, true);

            useAttr(CP_BORDER, false);
            mvhline(startY + 1, x, ACS_HLINE, colW);
            if (c > 0)
                mvvline(startY + 1, x - 1, ACS_VLINE, bodyH);
            if (c == 2)
                mvvline(startY + 1, x + colW, ACS_VLINE, bodyH);
            offAttr(CP_BORDER, false);

            int visible = bodyH - 1;
            vector<Task*>& items = kanbanItems[c];

            if (kanbanRow[c] >= static_cast<int>(items.size()))
                kanbanRow[c] = static_cast<int>(items.size()) - 1;
            if (kanbanRow[c] < 0) kanbanRow[c] = 0;

            int scroll = 0;
            if (kanbanRow[c] >= visible)
                scroll = kanbanRow[c] - visible + 1;

            for (int row = 0; row < visible; ++row) {
                int idx = scroll + row;
                if (idx >= static_cast<int>(items.size())) break;

                int y = startY + 2 + row;
                bool selected = (c == kanbanCol && idx == kanbanRow[c]);

                if (selected) useAttr(CP_SELECTED, true);
                else useAttr(CP_NORMAL, false);

                string cell = items[idx]->displayName()
                            + " [" + items[idx]->formatAgo() + "]";
                printUtf8At(y, x + 1, fitPad(cell, static_cast<size_t>(colW - 2)));

                if (selected) offAttr(CP_SELECTED, true);
                else offAttr(CP_NORMAL, false);
            }
        }
    }

    void rebuildKanban() {
        for (int i = 0; i < 3; ++i)
            kanbanItems[i].clear();

        vector<Task*> all;
        collectAll(all, rootTasks);

        for (size_t i = 0; i < all.size(); ++i) {
            int group = kanbanMode == 0
                ? static_cast<int>(all[i]->difficulty)
                : static_cast<int>(all[i]->urgency);
            if (group >= 0 && group < 3)
                kanbanItems[group].push_back(all[i]);
        }

        for (int i = 0; i < 3; ++i) {
            if (kanbanRow[i] >= static_cast<int>(kanbanItems[i].size()))
                kanbanRow[i] = static_cast<int>(kanbanItems[i].size()) - 1;
            if (kanbanRow[i] < 0) kanbanRow[i] = 0;
        }
    }

    void drawConfirm() {
        string title;
        string text;

        if (confirmKind == CONF_DELETE_LIST ||
            confirmKind == CONF_DELETE_DETAIL) {
            title = " УДАЛЕНИЕ ";
            Task* t = findById(confirmTaskId, rootTasks);
            if (t) {
                text = "Удалить \"" + t->displayName() + "\"?";
                int count = t->countSubtree();
                if (count > 1)
                    text += " Вместе с вложенными: " + intToStr(count) + " задач.";
            } else {
                text = "Задача больше не существует.";
            }
        } else if (confirmKind == CONF_DELETE_HISTORY) {
            title = " УДАЛЕНИЕ ОТМЕТКИ ";
            Task* t = findById(historyTaskId, rootTasks);
            if (t && confirmTaskId >= 0 &&
                confirmTaskId < static_cast<int>(t->history.size())) {
                text = "Удалить отметку от " +
                       formatStamp(t->history[confirmTaskId]) + "?";
            } else {
                text = "Отметка больше не существует.";
            }
        } else if (confirmKind == CONF_RELOAD) {
            title = " ПЕРЕЗАГРУЗКА ";
            text = "Есть несохранённые изменения. Перечитать файл и потерять их?";
        } else if (confirmKind == CONF_EXIT) {
            title = " ВЫХОД ";
            text = "Сохранить изменения перед выходом?";
        } else {
            title = " ПОДТВЕРЖДЕНИЕ ";
            text = "Продолжить?";
        }

        vector<string> wrapped = wrapText(text, static_cast<size_t>(max(12, cols - 12)), 4);

        int boxW = min(cols - 4, max(40, static_cast<int>(utf8Len(text)) + 8));
        if (boxW < 40) boxW = 40;

        int boxH = static_cast<int>(wrapped.size()) + 6;
        if (boxH > rows - 2) boxH = rows - 2;

        int y = (rows - boxH) / 2;
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        useAttr(CP_HEADER, true);
        printUtf8At(y, x + max(2, (boxW - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        useAttr(CP_NORMAL, false);
        int ty = y + 2;
        for (size_t i = 0; i < wrapped.size() && ty < y + boxH - 3; ++i, ++ty)
            printUtf8At(ty, x + 2, fitOutput(wrapped[i], static_cast<size_t>(boxW - 4)));
        offAttr(CP_NORMAL, false);

        string no = "[ Нет ]";
        string yes = "[ Да ]";
        int optionY = y + boxH - 2;

        if (confirmKind == CONF_EXIT) {
            no = "[ Остаться ]";
            yes = "[ Выйти ]";
        }

        if (messageReturnIndex == 0) useAttr(CP_SELECTED, true);
        else useAttr(CP_NORMAL, false);
        printUtf8At(optionY, x + boxW / 2 - 12, no);
        if (messageReturnIndex == 0) offAttr(CP_SELECTED, true);
        else offAttr(CP_NORMAL, false);

        if (messageReturnIndex == 1) useAttr(CP_SELECTED, true);
        else useAttr(CP_NORMAL, false);
        printUtf8At(optionY, x + boxW / 2 + 2, yes);
        if (messageReturnIndex == 1) offAttr(CP_SELECTED, true);
        else offAttr(CP_NORMAL, false);
    }

    void drawMessage() {
        vector<string> wrapped =
            wrapText(messageText, static_cast<size_t>(max(12, cols - 12)), 5);

        int boxW = min(cols - 4,
                       max(36, static_cast<int>(utf8Len(messageText)) + 8));
        if (boxW < 36) boxW = 36;

        int boxH = static_cast<int>(wrapped.size()) + 5;
        if (boxH > rows - 2) boxH = rows - 2;

        int y = (rows - boxH) / 2;
        int x = (cols - boxW) / 2;

        drawBox(y, x, boxH, boxW);

        useAttr(CP_HEADER, true);
        string title = " СООБЩЕНИЕ ";
        printUtf8At(y, x + max(2, (boxW - static_cast<int>(utf8Len(title))) / 2), title);
        offAttr(CP_HEADER, true);

        useAttr(CP_NORMAL, false);
        int ty = y + 2;
        for (size_t i = 0; i < wrapped.size() && ty < y + boxH - 2; ++i, ++ty)
            printUtf8At(ty, x + 2, fitOutput(wrapped[i], static_cast<size_t>(boxW - 4)));
        offAttr(CP_NORMAL, false);

        useAttr(CP_SELECTED, true);
        mvaddstr(y + boxH - 2, x + (boxW - 6) / 2, "[ OK ]");
        offAttr(CP_SELECTED, true);
    }

    // ===================== INPUT / FORMS =====================

    void appendInputByte(string& value, int ch) {
        if (value.size() >= 16384) return;

        if (ch >= 32 && ch <= 255) {
            value.push_back(static_cast<char>(ch));
            return;
        }

        if (ch >= 0x80 && ch <= 0x10FFFF &&
            !(ch >= 0xD800 && ch <= 0xDFFF)) {
            unsigned long cp = static_cast<unsigned long>(ch);
            if (cp <= 0x7FFUL) {
                value.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else if (cp <= 0xFFFFUL) {
                value.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                value.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
    }

    void backspaceUtf8(string& value) {
        if (value.empty()) return;

        size_t i = value.size() - 1;
        while (i > 0 &&
               (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80)
            --i;

        value.erase(i);
    }

    void beginAddForm(int parentId) {
        formEdit = false;
        formParentId = parentId;
        formField = 0;
        formFieldCount = 8;

        fName.clear();
        fDesc.clear();
        fLast.clear();
        fDue.clear();
        fHasLast = false;
        fHasDue = false;
        fDiff = DIFF_HIGH;
        fUrg = URG_LOW;

        screen = SCR_FORM;
        curs_set(1);
    }

    void beginEditForm(int taskId) {
        Task* t = findById(taskId, rootTasks);
        if (!t) return;

        formEdit = true;
        formParentId = -1;
        formField = 0;
        formFieldCount = 6;

        fName = t->name;
        fDesc = t->description;
        fLast.clear();
        fDue = t->hasDueDate ? t->formatDue() : "";
        fHasLast = false;
        fHasDue = t->hasDueDate;
        fDiff = t->difficulty;
        fUrg = t->urgency;

        detailTaskId = taskId;
        screen = SCR_FORM;
        curs_set(1);
    }

    void saveForm() {
        if (trimStr(fName).empty()) {
            showMessage("Название обязательно.", SCR_FORM);
            return;
        }

        time_t last = 0;
        time_t due = 0;

        if (!formEdit && fHasLast) {
            if (trimStr(fLast).empty() ||
                !parseDateString(trimStr(fLast), last)) {
                showMessage("Неверная дата последнего выполнения. Формат: ДД.ММ.ГГГГ ЧЧ:ММ",
                            SCR_FORM);
                return;
            }

            if (difftime(last, time(0)) > 0) {
                showMessage("Дата последнего выполнения не может быть в будущем.",
                            SCR_FORM);
                return;
            }
        }

        if (fHasDue) {
            if (trimStr(fDue).empty() ||
                !parseDateString(trimStr(fDue), due)) {
                showMessage("Неверная дата срока. Формат: ДД.ММ.ГГГГ ЧЧ:ММ",
                            SCR_FORM);
                return;
            }
        }

        if (formEdit) {
            Task* t = findById(detailTaskId, rootTasks);
            if (!t) {
                curs_set(0);
                screen = SCR_LIST;
                return;
            }

            t->name = fName;
            t->description = fDesc;
            t->hasDueDate = fHasDue;
            t->dueDate = fHasDue ? due : 0;
            t->difficulty = fDiff;
            t->urgency = fUrg;
            g_dirty = true;

            curs_set(0);
            showMessage("Изменения сохранены.", SCR_DETAIL);
        } else {
            Task t;
            t.id = nextId++;
            t.name = fName;
            t.description = fDesc;
            t.hasDueDate = fHasDue;
            t.dueDate = fHasDue ? due : 0;
            t.difficulty = fDiff;
            t.urgency = fUrg;

            if (fHasLast)
                t.addCompletion(last);

            if (formParentId >= 0) {
                Task* parent = findById(formParentId, rootTasks);
                if (parent) {
                    parent->subtasks.push_back(t);
                    parent->expanded = true;
                } else {
                    rootTasks.push_back(t);
                }
            } else {
                rootTasks.push_back(t);
            }

            g_dirty = true;
            rebuildFlat();
            curs_set(0);

            if (formParentId >= 0)
                showMessage("Подзадача добавлена.", SCR_DETAIL);
            else
                showMessage("Задача добавлена.", SCR_LIST);
        }
    }

    // ===================== NAVIGATION =====================

    void handleMenu(int ch) {
        switch (ch) {
            case KEY_UP:
            case 'k':
                menuSel = (menuSel + 6) % 7;
                break;

            case KEY_DOWN:
            case 'j':
                menuSel = (menuSel + 1) % 7;
                break;

            case '\n':
            case KEY_ENTER:
            case ' ':
                activateMenu();
                break;

            case 's':
            case 'S':
                if (saveData())
                    showMessage("Сохранено.", SCR_MENU);
                else
                    showMessage(g_ioError, SCR_MENU);
                break;

            case 'r':
            case 'R':
                if (g_dirty) {
                    confirmKind = CONF_RELOAD;
                    messageReturnIndex = 0;
                    screen = SCR_CONFIRM;
                } else {
                    reloadData();
                }
                break;

            case 'q':
            case 'Q':
                if (g_dirty) {
                    confirmKind = CONF_EXIT;
                    messageReturnIndex = 0;
                    screen = SCR_CONFIRM;
                } else {
                    screen = -1;
                }
                break;

            case KEY_RESIZE:
                break;
        }
    }

    void activateMenu() {
        switch (menuSel) {
            case 0:
                enterList();
                break;
            case 1:
                beginAddForm(-1);
                break;
            case 2:
                enterKanban(0);
                break;
            case 3:
                enterKanban(1);
                break;
            case 4:
                if (saveData())
                    showMessage("Сохранено.", SCR_MENU);
                else
                    showMessage(g_ioError, SCR_MENU);
                break;
            case 5:
                if (g_dirty) {
                    confirmKind = CONF_RELOAD;
                    messageReturnIndex = 0;
                    screen = SCR_CONFIRM;
                } else {
                    reloadData();
                }
                break;
            case 6:
                if (g_dirty) {
                    confirmKind = CONF_EXIT;
                    messageReturnIndex = 0;
                    screen = SCR_CONFIRM;
                } else {
                    screen = -1;
                }
                break;
        }
    }

    void enterList() {
        screen = SCR_LIST;
        listSel = 0;
        listScroll = 0;
        rebuildFlat();
    }

    void handleList(int ch) {
        rebuildFlat();

        if (flat.empty()) {
            if (ch == 'a' || ch == 'A' || ch == KEY_IC) {
                beginAddForm(-1);
                return;
            }
            if (ch == 27 || ch == 'b' || ch == 'B') {
                screen = SCR_MENU;
            }
            return;
        }

        switch (ch) {
            case KEY_UP:
            case 'k':
                listSel = (listSel + static_cast<int>(flat.size()) - 1)
                        % static_cast<int>(flat.size());
                adjustListScroll();
                break;

            case KEY_DOWN:
            case 'j':
                listSel = (listSel + 1) % static_cast<int>(flat.size());
                adjustListScroll();
                break;

            case '\n':
            case KEY_ENTER:
                detailTaskId = flat[listSel].second;
                screen = SCR_DETAIL;
                break;

            case KEY_LEFT:
            case 'h':
            case '-':
                setExpanded(flat[listSel].second, false);
                rebuildFlat();
                break;

            case KEY_RIGHT:
            case 'l':
            case '+':
                setExpanded(flat[listSel].second, true);
                rebuildFlat();
                break;

            case 'x':
            case 'X':
                completeTask(flat[listSel].second);
                break;

            case 'a':
            case 'A':
            case KEY_IC:
                beginAddForm(flat[listSel].second);
                break;

            case 'e':
            case 'E':
                beginEditForm(flat[listSel].second);
                break;

            case 'd':
            case 'D':
            case KEY_DC:
                confirmKind = CONF_DELETE_LIST;
                confirmTaskId = flat[listSel].second;
                messageReturnIndex = 0;
                screen = SCR_CONFIRM;
                break;

            case 27:
            case 'b':
            case 'B':
                screen = SCR_MENU;
                break;

            case KEY_RESIZE:
                break;
        }
    }

    void setExpanded(int taskId, bool value) {
        Task* t = findById(taskId, rootTasks);
        if (t == 0 || t->subtasks.empty()) return;
        t->expanded = value;
        g_dirty = true;
    }

    void completeTask(int taskId) {
        Task* t = findById(taskId, rootTasks);
        if (t == 0) return;

        t->addCompletion(time(0));
        g_dirty = true;
        showMessage("Отмечено выполненным.", SCR_LIST);
    }

    void handleDetail(int ch) {
        Task* t = findById(detailTaskId, rootTasks);
        if (!t) {
            screen = SCR_LIST;
            return;
        }

        switch (ch) {
            case '1':
                completeTaskFromDetail();
                break;

            case '2':
                beginAddForm(detailTaskId);
                break;

            case '3':
                beginEditForm(detailTaskId);
                break;

            case '4':
                confirmKind = CONF_DELETE_DETAIL;
                confirmTaskId = detailTaskId;
                messageReturnIndex = 0;
                screen = SCR_CONFIRM;
                break;

            case '5':
                historyTaskId = detailTaskId;
                historySel = 0;
                historyScroll = 0;
                screen = SCR_HISTORY;
                break;

            case 27:
            case 'b':
            case 'B':
                enterList();
                break;
        }
    }

    void completeTaskFromDetail() {
        Task* t = findById(detailTaskId, rootTasks);
        if (!t) return;

        t->addCompletion(time(0));
        g_dirty = true;
        showMessage("Отмечено выполненным.", SCR_DETAIL);
    }

    void handleForm(int ch) {
        if (ch == 27 || (ch == 'b' || ch == 'B') && !isFormTextField(formField)) {
            curs_set(0);
            if (formEdit) screen = SCR_DETAIL;
            else if (formParentId >= 0) screen = SCR_DETAIL;
            else screen = SCR_LIST;
            return;
        }

        if (ch == '\t' || ch == KEY_DOWN) {
            formField = (formField + 1) % formFieldCount;
            return;
        }

        if (ch == KEY_BTAB || ch == KEY_UP) {
            formField = (formField + formFieldCount - 1) % formFieldCount;
            return;
        }

        if (ch == KEY_LEFT || ch == KEY_RIGHT) {
            if (!isFormTextField(formField)) {
                toggleFormValue(formField, ch == KEY_RIGHT ? 1 : -1);
            }
            return;
        }

        if (ch == ' ') {
            if (!isFormTextField(formField)) {
                toggleFormValue(formField, 1);
            } else {
                appendInputByte(formTextRef(formField), ch);
            }
            return;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (isFormTextField(formField))
                backspaceUtf8(formTextRef(formField));
            return;
        }

        if (ch == '\n' || ch == KEY_ENTER) {
            saveForm();
            return;
        }

        if (isFormTextField(formField))
            appendInputByte(formTextRef(formField), ch);
    }

    // ===================== HISTORY =====================

    void handleHistory(int ch) {
        Task* t = findById(historyTaskId, rootTasks);
        if (!t) {
            screen = SCR_LIST;
            return;
        }

        if (t->history.empty()) {
            if (ch == 'a' || ch == 'A') {
                dateInput.clear();
                screen = SCR_HISTORY_ADD;
                curs_set(1);
                    } else if (ch == 27 || ch == 'b' || ch == 'B') {
                screen = SCR_DETAIL;
            }
            return;
        }

        switch (ch) {
            case KEY_UP:
            case 'k':
                historySel = (historySel + static_cast<int>(t->history.size()) - 1)
                           % static_cast<int>(t->history.size());
                break;

            case KEY_DOWN:
            case 'j':
                historySel = (historySel + 1)
                           % static_cast<int>(t->history.size());
                break;

            case 'a':
            case 'A':
                dateInput.clear();
                screen = SCR_HISTORY_ADD;
                curs_set(1);
                break;

            case 'd':
            case 'D':
            case KEY_DC:
                confirmKind = CONF_DELETE_HISTORY;
                confirmTaskId = historySel;
                messageReturnIndex = 0;
                screen = SCR_CONFIRM;
                break;

            case 27:
            case 'b':
            case 'B':
                screen = SCR_DETAIL;
                break;
        }
    }

    void handleHistoryAdd(int ch) {
        if (ch == 27 || ch == 'b' || ch == 'B') {
            curs_set(0);
            screen = SCR_HISTORY;
            return;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            backspaceUtf8(dateInput);
            return;
        }

        if (ch == '\n' || ch == KEY_ENTER) {
            time_t when;
            if (!parseDateString(trimStr(dateInput), when)) {
                showMessage("Неверная дата. Формат: ДД.ММ.ГГГГ ЧЧ:ММ",
                            SCR_HISTORY_ADD);
                return;
            }

            if (difftime(when, time(0)) > 0) {
                showMessage("Дата выполнения не может быть в будущем.",
                            SCR_HISTORY_ADD);
                return;
            }

            Task* t = findById(historyTaskId, rootTasks);
            if (!t) {
                curs_set(0);
                screen = SCR_LIST;
                return;
            }

            if (t->history.size() >= static_cast<size_t>(MAX_HISTORY)) {
                curs_set(0);
                showMessage("Достигнут предел истории.", SCR_HISTORY);
                return;
            }

            t->addCompletion(when);
            g_dirty = true;

            if (historySel >= static_cast<int>(t->history.size()))
                historySel = static_cast<int>(t->history.size()) - 1;
            if (historySel < 0) historySel = 0;

            curs_set(0);
            screen = SCR_HISTORY;
            return;
        }

        appendInputByte(dateInput, ch);
    }

    // ===================== KANBAN =====================

    void enterKanban(int mode) {
        kanbanMode = mode;
        kanbanCol = 0;
        kanbanRow[0] = kanbanRow[1] = kanbanRow[2] = 0;
        rebuildKanban();
        screen = SCR_KANBAN;
    }

    void handleKanban(int ch) {
        switch (ch) {
            case KEY_LEFT:
            case 'h':
                kanbanCol = (kanbanCol + 2) % 3;
                break;

            case KEY_RIGHT:
            case 'l':
                kanbanCol = (kanbanCol + 1) % 3;
                break;

            case KEY_UP:
            case 'k':
                if (kanbanRow[kanbanCol] > 0)
                    --kanbanRow[kanbanCol];
                break;

            case KEY_DOWN:
            case 'j':
                if (kanbanRow[kanbanCol] + 1 <
                    static_cast<int>(kanbanItems[kanbanCol].size()))
                    ++kanbanRow[kanbanCol];
                break;

            case '\t':
                enterKanban(kanbanMode == 0 ? 1 : 0);
                break;

            case '\n':
            case KEY_ENTER:
                if (!kanbanItems[kanbanCol].empty() &&
                    kanbanRow[kanbanCol] <
                    static_cast<int>(kanbanItems[kanbanCol].size())) {
                    detailTaskId =
                        kanbanItems[kanbanCol][kanbanRow[kanbanCol]]->id;
                    screen = SCR_DETAIL;
                }
                break;

            case 27:
            case 'b':
            case 'B':
                screen = SCR_MENU;
                break;
        }
    }

    // ===================== CONFIRM / MESSAGE =====================

    void handleConfirm(int ch) {
        if (ch == KEY_LEFT || ch == 'h') {
            messageReturnIndex = 0;
            return;
        }

        if (ch == KEY_RIGHT || ch == 'l') {
            messageReturnIndex = 1;
            return;
        }

        if (ch == 'y' || ch == 'Y') {
            messageReturnIndex = 1;
            return;
        }

        if (ch == 'n' || ch == 'N' || ch == 27 || ch == 'b' || ch == 'B') {
            ConfirmKind kind = confirmKind;
            messageReturnIndex = 0;
            confirmKind = CONF_NONE;

            if (kind == CONF_DELETE_LIST)
                screen = SCR_LIST;
            else if (kind == CONF_DELETE_DETAIL)
                screen = SCR_DETAIL;
            else if (kind == CONF_DELETE_HISTORY)
                screen = SCR_HISTORY;
            else
                screen = SCR_MENU;
            return;
        }

        if (ch != '\n' && ch != KEY_ENTER)
            return;

        bool yes = (messageReturnIndex == 1);
        ConfirmKind kind = confirmKind;
        confirmKind = CONF_NONE;

        if (kind == CONF_DELETE_LIST || kind == CONF_DELETE_DETAIL) {
            if (!yes) {
                if (kind == CONF_DELETE_LIST)
                    screen = SCR_LIST;
                else
                    screen = SCR_DETAIL;
                return;
            }

            Task* t = findById(confirmTaskId, rootTasks);
            int removedCount = t ? t->countSubtree() : 0;

            if (t && removeById(confirmTaskId, rootTasks)) {
                g_dirty = true;
                if (kind == CONF_DELETE_LIST) {
                    rebuildFlat();
                    if (listSel >= static_cast<int>(flat.size()))
                        listSel = static_cast<int>(flat.size()) - 1;
                    if (listSel < 0) listSel = 0;
                    showMessage(
                        "Удалено задач: " +
                        intToStr(removedCount) + ".",
                        SCR_LIST);
                } else {
                    enterList();
                    showMessage(
                        "Удалено задач: " +
                        intToStr(removedCount) + ".",
                        SCR_LIST);
                }
            } else {
                screen = SCR_LIST;
            }
            return;
        }

        if (kind == CONF_DELETE_HISTORY) {
            if (!yes) {
                screen = SCR_HISTORY;
                return;
            }

            Task* t = findById(historyTaskId, rootTasks);
            if (t && confirmTaskId >= 0 &&
                confirmTaskId < static_cast<int>(t->history.size())) {
                t->history.erase(t->history.begin() + confirmTaskId);
                g_dirty = true;

                if (historySel >= static_cast<int>(t->history.size()))
                    historySel = static_cast<int>(t->history.size()) - 1;
                if (historySel < 0) historySel = 0;
            }

            screen = SCR_HISTORY;
            return;
        }

        if (kind == CONF_RELOAD) {
            if (!yes) {
                screen = SCR_MENU;
                return;
            }

            reloadData();
            return;
        }

        if (kind == CONF_EXIT) {
            if (!yes) {
                screen = SCR_MENU;
                return;
            }

            if (saveData()) {
                screen = -1;
            } else {
                showMessage(
                    "Не удалось сохранить. Выход отменён.\n" + g_ioError,
                    SCR_MENU);
            }
            return;
        }
    }

    void handleMessage(int ch) {
        if (ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == 'b' || ch == 'B' || ch == ' ') {
            curs_set(0);
            screen = messageReturnScreen;
        }
    }

    void showMessage(const string& text, int returnScreen) {
        messageText = displaySafe(text);
        messageReturnScreen = returnScreen;
        screen = SCR_MESSAGE;
        curs_set(0);
    }

    void reloadData() {
        if (loadData()) {
            if (!g_ioError.empty()) {
                showMessage(g_ioError, SCR_MENU);
                g_ioError.clear();
            } else {
                showMessage("Данные перечитаны с диска.", SCR_MENU);
            }
        } else {
            showMessage(g_ioError, SCR_MENU);
        }
    }
};

int main() {
    Application app;
    return app.run();
}
