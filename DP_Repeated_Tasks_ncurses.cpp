// RepeatedTasks — ncurses edition for C4Droid / Android
// Compile: g++ -std=c++11 -o RepeatedTasks RepeatedTasks.cpp -lncurses
// Для C4Droid: в настройках линковщика укажите -lncurses

#include <ncurses.h>
#include <string>
#include <vector>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <clocale>
#include <functional>

using namespace std;

// ======================== CONSTANTS ========================
const char* DATA_FILE = "repeated_tasks.dat";

#define SCR_MENU        0
#define SCR_LIST        1
#define SCR_DETAIL      2
#define SCR_FORM        3
#define SCR_KANBAN      4
#define SCR_CONFIRM     5
#define SCR_MSG         6

// Color pairs
#define CP_HEADER       1
#define CP_NORMAL       2
#define CP_SELECTED     3
#define CP_HIGH         4
#define CP_MEDIUM       5
#define CP_LOW          6
#define CP_SECONDARY    7
#define CP_DANGER       8
#define CP_BORDER       9

// ======================== ENUMS ========================
enum Difficulty { DIFF_HIGH = 0, DIFF_MEDIUM = 1, DIFF_LOW = 2 };
enum Urgency    { URG_HIGH = 0,  URG_MEDIUM = 1,  URG_LOW = 2 };

const char* diffName(int d) {
    static const char* names[] = {"Высокая", "Средняя", "Низкая"};
    return names[d];
}
const char* urgName(int u) {
    static const char* names[] = {"Высокая", "Средняя", "Низкая"};
    return names[u];
}

// ======================== TASK MODEL ========================
struct Task {
    int id;
    string name;
    string description;
    time_t lastDone;
    time_t dueDate;
    bool hasDueDate;
    int difficulty; // 0,1,2
    int urgency;    // 0,1,2
    bool expanded;
    vector<Task> subtasks;

    Task() : id(0), lastDone(time(nullptr)), dueDate(0), hasDueDate(false),
             difficulty(DIFF_HIGH), urgency(URG_LOW), expanded(true) {}
};

// ======================== HELPERS ========================
string toStr(int x) {
    return to_string(x);
}

string escapeStr(const string& s) {
    string r;
    r.reserve(s.size() * 2);
    for (char c : s) {
        if (c == '\n') r += "\\n";
        else if (c == '\\') r += "\\\\";
        else r += c;
    }
    return r;
}

string unescapeStr(const string& s) {
    string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i+1] == 'n') { r += '\n'; ++i; }
            else if (s[i+1] == '\\') { r += '\\'; ++i; }
            else r += s[i];
        } else r += s[i];
    }
    return r;
}

string formatAgo(time_t last) {
    time_t now = time(nullptr);
    double diff = difftime(now, last);
    if (diff < 0) diff = 0;
    int totalHours = (int)(diff / 3600.0);
    int hours = totalHours % 24;
    int totalDays = totalHours / 24;

    if (diff < 86400) {
        if (totalHours == 0) return "<1ч";
        return toStr(totalHours) + "ч";
    }
    if (diff < 2592000) {
        return toStr(totalDays) + "д " + toStr(hours) + "ч";
    }
    int years = totalDays / 365;
    int months = (totalDays % 365) / 30;
    int days = (totalDays % 365) % 30;
    if (years > 0) {
        return toStr(years) + "г " + toStr(months) + "м " + toStr(days) + "д " + toStr(hours) + "ч";
    }
    return toStr(months) + "м " + toStr(days) + "д " + toStr(hours) + "ч";
}

string formatDate(time_t t) {
    tm* ti = localtime(&t);
    if (!ti) return "-";
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", ti);
    return string(buf);
}

bool parseDate(const string& s, time_t& out) {
    int d, m, y, h, mn;
    if (sscanf(s.c_str(), "%d.%d.%d %d:%d", &d, &m, &y, &h, &mn) != 5) return false;
    tm tm = {};
    tm.tm_mday = d;
    tm.tm_mon = m - 1;
    tm.tm_year = y - 1900;
    tm.tm_hour = h;
    tm.tm_min = mn;
    tm.tm_isdst = -1;
    time_t r = mktime(&tm);
    if (r == (time_t)-1) return false;
    out = r;
    return true;
}

// ======================== STORAGE ========================
class Storage {
    static void saveTask(ofstream& out, const Task& t) {
        out << "BEGIN_TASK\n";
        out << "id:" << t.id << "\n";
        out << "name:" << escapeStr(t.name) << "\n";
        out << "desc:" << escapeStr(t.description) << "\n";
        out << "last:" << (long long)t.lastDone << "\n";
        out << "due:" << (long long)t.dueDate << "\n";
        out << "hasDue:" << (t.hasDueDate ? 1 : 0) << "\n";
        out << "diff:" << t.difficulty << "\n";
        out << "urg:" << t.urgency << "\n";
        out << "expanded:" << (t.expanded ? 1 : 0) << "\n";
        out << "subcount:" << t.subtasks.size() << "\n";
        for (const auto& st : t.subtasks) saveTask(out, st);
        out << "END_TASK\n";
    }

    static void loadTask(ifstream& in, Task& t) {
        string line, val;
        auto readVal = [&](const string& prefix) -> string {
            getline(in, line);
            if (line.find(prefix) == 0) return line.substr(prefix.size());
            return "";
        };
        getline(in, line); // BEGIN_TASK
        t.id = atoi(readVal("id:").c_str());
        t.name = unescapeStr(readVal("name:"));
        t.description = unescapeStr(readVal("desc:"));
        t.lastDone = (time_t)atoll(readVal("last:").c_str());
        t.dueDate = (time_t)atoll(readVal("due:").c_str());
        t.hasDueDate = atoi(readVal("hasDue:").c_str()) != 0;
        t.difficulty = atoi(readVal("diff:").c_str());
        t.urgency = atoi(readVal("urg:").c_str());
        t.expanded = atoi(readVal("expanded:").c_str()) != 0;
        int cnt = atoi(readVal("subcount:").c_str());
        t.subtasks.clear();
        for (int i = 0; i < cnt; ++i) {
            Task st;
            loadTask(in, st);
            t.subtasks.push_back(st);
        }
        getline(in, line); // END_TASK
    }

public:
    static bool save(const vector<Task>& roots, int nextId) {
        string tmp = string(DATA_FILE) + ".tmp";
        ofstream out(tmp);
        if (!out) return false;
        out << "REPEATED_TASKS_V2\n";
        out << "nextId:" << nextId << "\n";
        out << "rootCount:" << roots.size() << "\n";
        for (const auto& t : roots) saveTask(out, t);
        out.close();
        remove(DATA_FILE);
        rename(tmp.c_str(), DATA_FILE);
        return true;
    }

    static bool load(vector<Task>& roots, int& nextId) {
        ifstream in(DATA_FILE);
        if (!in) return false;
        string line;
        getline(in, line);
        if (line != "REPEATED_TASKS_V2") return false;
        getline(in, line);
        nextId = atoi(line.substr(7).c_str());
        getline(in, line);
        int cnt = atoi(line.substr(11).c_str());
        roots.clear();
        for (int i = 0; i < cnt; ++i) {
            Task t;
            loadTask(in, t);
            roots.push_back(t);
        }
        return true;
    }
};

// ======================== FLAT ITEM ========================
struct FlatItem {
    Task* task;
    int level;
};

// ======================== MANAGER ========================
class TaskManager {
public:
    vector<Task> roots;
    int nextId = 1;

    void flatten(vector<FlatItem>& out, vector<Task>& src, int level = 0) {
        for (auto& t : src) {
            out.push_back({&t, level});
            if (t.expanded) flatten(out, t.subtasks, level + 1);
        }
    }

    void flattenAll(vector<FlatItem>& out) { flatten(out, roots); }

    Task* findById(int id, vector<Task>& tasks) {
        for (auto& t : tasks) {
            if (t.id == id) return &t;
            Task* p = findById(id, t.subtasks);
            if (p) return p;
        }
        return nullptr;
    }

    Task* findById(int id) { return findById(id, roots); }

    bool removeById(int id, vector<Task>& tasks) {
        for (size_t i = 0; i < tasks.size(); ++i) {
            if (tasks[i].id == id) {
                tasks.erase(tasks.begin() + i);
                return true;
            }
            if (removeById(id, tasks[i].subtasks)) return true;
        }
        return false;
    }

    bool removeById(int id) { return removeById(id, roots); }

    int countSubtasks(const Task& t) const {
        int c = t.subtasks.size();
        for (const auto& st : t.subtasks) c += countSubtasks(st);
        return c;
    }

    void collectByDiff(int diff, vector<Task*>& out, vector<Task>& tasks) {
        for (auto& t : tasks) {
            if (t.difficulty == diff) out.push_back(&t);
            collectByDiff(diff, out, t.subtasks);
        }
    }
    void collectByUrg(int urg, vector<Task*>& out, vector<Task>& tasks) {
        for (auto& t : tasks) {
            if (t.urgency == urg) out.push_back(&t);
            collectByUrg(urg, out, t.subtasks);
        }
    }

    void save() { Storage::save(roots, nextId); }
    void load() { Storage::load(roots, nextId); }
};

// ======================== APPLICATION ========================
class Application {
    TaskManager tm;
    int screen = SCR_MENU;
    int rows = 0, cols = 0;

    // Menu
    int menuSel = 0;
    const char* menuItems[6] = {
        "Список задач",
        "Добавить корневую задачу",
        "Канбан: Сложность",
        "Канбан: Срочность",
        "Сохранить на диск",
        "Выйти (с сохранением)"
    };

    // Task list
    vector<FlatItem> flat;
    int listSel = 0;
    int listScroll = 0;

    // Detail
    int detailTaskId = -1;

    // Form
    bool formEdit = false;
    int formParentId = -1; // -1 = root
    string f_name, f_desc, f_date;
    bool f_hasDue;
    int f_diff, f_urg;
    int formField = 0;
    static const int FORM_FIELDS = 6;

    // Kanban
    int kanbanMode = 0; // 0=diff, 1=urg
    int kanbanCol = 0;
    int kanbanRow[3] = {0, 0, 0};
    vector<Task*> kanbanItems[3];

    // Confirm
    string confirmTitle;
    string confirmText;
    function<void()> confirmYes;
    int confirmSel = 0; // 0=No, 1=Yes

    // Message
    string msgText;
    int msgNextScreen = SCR_MENU;

public:
    void run() {
        // Принудительная установка локали UTF-8
        setlocale(LC_ALL, "C.UTF-8");
        setlocale(LC_ALL, "");

        initscr();
        cbreak();
        noecho();
        keypad(stdscr, TRUE);
        curs_set(0);

        if (has_colors()) {
            start_color();
            initColors();
        }
        tm.load();
        while (screen != -1) {
            getmaxyx(stdscr, rows, cols);
            if (rows < 15 || cols < 50) {
                clear();
                mvprintw(0, 0, "Терминал слишком мал: %dx%d. Нужно минимум 50x15.", cols, rows);
                refresh();
                if (getch() == 'q') break;
                continue;
            }
            clear();
            switch (screen) {
                case SCR_MENU:    drawMenu(); break;
                case SCR_LIST:    drawTaskList(); break;
                case SCR_DETAIL:  drawDetail(); break;
                case SCR_FORM:    drawForm(); break;
                case SCR_KANBAN:  drawKanban(); break;
                case SCR_CONFIRM: drawConfirm(); break;
                case SCR_MSG:     drawMessage(); break;
            }
            drawStatusBar();
            refresh();
            int ch = getch();
            switch (screen) {
                case SCR_MENU:    handleMenu(ch); break;
                case SCR_LIST:    handleList(ch); break;
                case SCR_DETAIL:  handleDetail(ch); break;
                case SCR_FORM:    handleForm(ch); break;
                case SCR_KANBAN:  handleKanban(ch); break;
                case SCR_CONFIRM: handleConfirm(ch); break;
                case SCR_MSG:     handleMessage(ch); break;
            }
        }
        endwin();
    }

private:
    void initColors() {
        init_pair(CP_HEADER,    COLOR_CYAN,    COLOR_BLACK);
        init_pair(CP_NORMAL,    COLOR_WHITE,   COLOR_BLACK);
        init_pair(CP_SELECTED,  COLOR_BLACK,   COLOR_WHITE);
        init_pair(CP_HIGH,      COLOR_RED,     COLOR_BLACK);
        init_pair(CP_MEDIUM,    COLOR_YELLOW,  COLOR_BLACK);
        init_pair(CP_LOW,       COLOR_GREEN,   COLOR_BLACK);
        init_pair(CP_SECONDARY, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(CP_DANGER,    COLOR_RED,     COLOR_BLACK);
        init_pair(CP_BORDER,    COLOR_BLUE,    COLOR_BLACK);
    }

    void drawBox(int y, int x, int h, int w) {
        attron(COLOR_PAIR(CP_BORDER));
        mvhline(y, x + 1, ACS_HLINE, w - 2);
        mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
        mvvline(y + 1, x, ACS_VLINE, h - 2);
        mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
        mvaddch(y, x, ACS_ULCORNER);
        mvaddch(y, x + w - 1, ACS_URCORNER);
        mvaddch(y + h - 1, x, ACS_LLCORNER);
        mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
        attroff(COLOR_PAIR(CP_BORDER));
    }

    void drawCenteredWindow(int h, int w, const string& title) {
        int y = (rows - h) / 2;
        int x = (cols - w) / 2;
        drawBox(y, x, h, w);
        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        int tl = title.size();
        int tx = x + (w - tl) / 2;
        mvprintw(y, tx, "%s", title.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
    }

    void drawStatusBar() {
        attron(COLOR_PAIR(CP_HEADER) | A_REVERSE);
        mvhline(rows - 1, 0, ' ', cols);
        string status;
        switch (screen) {
            case SCR_MENU:    status = " [↑↓] Выбор  [Enter] Открыть  [Q] Выход"; break;
            case SCR_LIST:    status = " [↑↓] Навигация  [Enter] Детали  [±] Свернуть  [Ins] Добавить  [Del] Удалить  [E] Ред.  [X] Выполнена  [Esc] Меню"; break;
            case SCR_DETAIL:  status = " [1] Выполнена  [2] Подзадача  [3] Редакт.  [4] Удалить  [Esc] Назад"; break;
            case SCR_FORM:    status = " [Tab] Поле  [↑↓] Значение  [Enter] Сохранить  [Esc] Отмена"; break;
            case SCR_KANBAN:  status = " [←→] Колонка  [↑↓] Задача  [Tab] Режим  [Enter] Детали  [Esc] Меню"; break;
            case SCR_CONFIRM: status = " [←→] Выбор  [Enter] Подтвердить  [Esc] Отмена"; break;
            case SCR_MSG:     status = " [Enter] OK"; break;
        }
        if ((int)status.size() > cols) status = status.substr(0, cols);
        mvprintw(rows - 1, 0, "%s", status.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_REVERSE);
    }

    // ===================== MENU =====================
    void drawMenu() {
        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        string title = " R e p e a t e d T a s k s ";
        int tx = (cols - title.size()) / 2;
        mvprintw(1, tx, "%s", title.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

        int boxY = 3;
        int boxH = 12;
        int boxW = 44;
        int boxX = (cols - boxW) / 2;
        drawBox(boxY, boxX, boxH, boxW);

        for (int i = 0; i < 6; ++i) {
            int y = boxY + 2 + i;
            int x = boxX + 4;
            if (i == menuSel) {
                attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
                string line = " > " + string(menuItems[i]);
                mvprintw(y, x, "%s", line.c_str());
                int len = line.size();
                for (int k = len; k < boxW - 6; ++k) addch(' ');
                attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            } else {
                attron(COLOR_PAIR(CP_NORMAL));
                string line = "   " + string(menuItems[i]);
                mvprintw(y, x, "%s", line.c_str());
                attroff(COLOR_PAIR(CP_NORMAL));
            }
        }

        attron(COLOR_PAIR(CP_SECONDARY));
        string info = "Корневых задач: " + toStr(tm.roots.size());
        mvprintw(boxY + boxH + 1, boxX, "%s", info.c_str());
        attroff(COLOR_PAIR(CP_SECONDARY));
    }

    void handleMenu(int ch) {
        switch (ch) {
            case KEY_UP: case 'k': menuSel = (menuSel + 5) % 6; break;
            case KEY_DOWN: case 'j': menuSel = (menuSel + 1) % 6; break;
            case '\n': case KEY_ENTER: case ' ':
                switch (menuSel) {
                    case 0: enterList(); break;
                    case 1: enterForm(false, -1); break;
                    case 2: enterKanban(0); break;
                    case 3: enterKanban(1); break;
                    case 4: tm.save(); showMessage("Сохранено!", SCR_MENU); break;
                    case 5: tm.save(); screen = -1; break;
                }
                break;
            case 'q': case 'Q': tm.save(); screen = -1; break;
        }
    }

    // ===================== LIST =====================
    void enterList() {
        screen = SCR_LIST;
        rebuildFlat();
        listSel = 0;
        listScroll = 0;
    }

    void rebuildFlat() {
        flat.clear();
        tm.flattenAll(flat);
    }

    void drawTaskList() {
        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        mvprintw(0, 2, "С П И С О К   З А Д А Ч");
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

        int contentH = rows - 3; // minus header and status
        int visible = contentH - 1;
        if (visible < 1) visible = 1;

        // Adjust scroll
        if (listSel < listScroll) listScroll = listSel;
        if (listSel >= listScroll + visible) listScroll = listSel - visible + 1;
        if (listScroll < 0) listScroll = 0;
        if (listScroll > (int)flat.size() - visible) listScroll = max(0, (int)flat.size() - visible);

        for (int i = 0; i < visible && listScroll + i < (int)flat.size(); ++i) {
            int idx = listScroll + i;
            const auto& item = flat[idx];
            int y = 1 + i;
            bool sel = (idx == listSel);

            if (sel) attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attron(COLOR_PAIR(CP_NORMAL));

            // Clear line
            mvhline(y, 0, ' ', cols);

            int x = 1 + item.level * 2;
            string prefix;
            if (!item.task->subtasks.empty())
                prefix = item.task->expanded ? "[-] " : "[+] ";
            else
                prefix = "[ ] ";

            string line = (sel ? "▶ " : "  ") + prefix + item.task->name;
            mvprintw(y, x, "%s", line.c_str());

            // Right side info
            string info = "[" + formatAgo(item.task->lastDone) + "]";
            info += " C:" + string(diffName(item.task->difficulty))[0];
            info += " U:" + string(urgName(item.task->urgency))[0];
            if (item.task->hasDueDate) info += " ->" + formatDate(item.task->dueDate);
            int ix = cols - info.size() - 2;
            if (ix < x + 20) ix = x + 20;
            if (ix + (int)info.size() < cols)
                mvprintw(y, ix, "%s", info.c_str());

            if (sel) attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attroff(COLOR_PAIR(CP_NORMAL));
        }

        // Scroll indicator
        if ((int)flat.size() > visible) {
            attron(COLOR_PAIR(CP_SECONDARY));
            int barH = visible;
            int pos = (listScroll * barH) / max(1, (int)flat.size() - visible);
            mvaddch(1 + pos, cols - 1, ACS_VLINE);
            attroff(COLOR_PAIR(CP_SECONDARY));
        }
    }

    void handleList(int ch) {
        switch (ch) {
            case KEY_UP: case 'k':
                if (flat.empty()) break;
                listSel = (listSel + flat.size() - 1) % flat.size();
                break;
            case KEY_DOWN: case 'j':
                if (flat.empty()) break;
                listSel = (listSel + 1) % flat.size();
                break;
            case KEY_RIGHT: case 'l': case '+':
                if (!flat.empty()) flat[listSel].task->expanded = true;
                rebuildFlat();
                break;
            case KEY_LEFT: case 'h': case '-':
                if (!flat.empty()) flat[listSel].task->expanded = false;
                rebuildFlat();
                break;
            case '\n': case KEY_ENTER:
                if (!flat.empty()) enterDetail(flat[listSel].task->id);
                break;
            case 'x': case 'X':
                if (!flat.empty()) {
                    flat[listSel].task->lastDone = time(nullptr);
                    showMessage("Отмечено выполненным!", SCR_LIST);
                }
                break;
            case 'i': case 'I': case KEY_IC: // Insert
                if (!flat.empty()) enterForm(false, flat[listSel].task->id);
                else enterForm(false, -1);
                break;
            case 'e': case 'E':
                if (!flat.empty()) enterForm(true, flat[listSel].task->id);
                break;
            case KEY_DC: case 'd': case 'D': // Delete
                if (!flat.empty()) {
                    Task* t = flat[listSel].task;
                    int subs = tm.countSubtasks(*t);
                    string txt = "Удалить \"" + t->name + "\"?";
                    if (subs > 0) txt += " (вместе с " + toStr(subs) + " подзадачами)";
                    askConfirm("Удаление", txt, [this, t]() {
                        tm.removeById(t->id);
                        rebuildFlat();
                        if (listSel >= (int)flat.size()) listSel = max(0, (int)flat.size() - 1);
                        showMessage("Удалено.", SCR_LIST);
                    });
                }
                break;
            case 27: // Esc
                screen = SCR_MENU;
                break;
        }
    }

    // ===================== DETAIL =====================
    void enterDetail(int id) {
        detailTaskId = id;
        screen = SCR_DETAIL;
    }

    void drawDetail() {
        Task* t = tm.findById(detailTaskId);
        if (!t) { screen = SCR_LIST; return; }

        int h = 14;
        int w = min(cols - 4, 60);
        int y = (rows - h) / 2;
        int x = (cols - w) / 2;
        drawBox(y, x, h, w);

        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        string title = " ЗАДАЧА #" + toStr(t->id) + " ";
        int tx = x + (w - title.size()) / 2;
        mvprintw(y, tx, "%s", title.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

        vector<pair<string,string>> lines = {
            {"Название:", t->name},
            {"Описание:", t->description.empty() ? "-" : t->description},
            {"Прошло:", formatAgo(t->lastDone) + " назад"},
            {"Срок:", t->hasDueDate ? formatDate(t->dueDate) : "-"},
            {"Сложность:", diffName(t->difficulty)},
            {"Срочность:", urgName(t->urgency)},
            {"Подзадач:", toStr(t->subtasks.size())}
        };

        for (size_t i = 0; i < lines.size(); ++i) {
            int ly = y + 2 + i;
            attron(COLOR_PAIR(CP_SECONDARY) | A_BOLD);
            mvprintw(ly, x + 2, "%s", lines[i].first.c_str());
            attroff(COLOR_PAIR(CP_SECONDARY) | A_BOLD);
            attron(COLOR_PAIR(CP_NORMAL));
            // Word wrap for description
            if (lines[i].first == "Описание:" && lines[i].second.size() > (size_t)(w - 16)) {
                string s = lines[i].second;
                int pos = 0, row = 0;
                while (pos < (int)s.size() && row < 2) {
                    int len = min(w - 16, (int)s.size() - pos);
                    mvprintw(ly + row, x + 14, "%s", s.substr(pos, len).c_str());
                    pos += len;
                    row++;
                }
            } else {
                mvprintw(ly, x + 14, "%s", lines[i].second.c_str());
            }
            attroff(COLOR_PAIR(CP_NORMAL));
        }

        // Actions hint
        attron(COLOR_PAIR(CP_LOW));
        mvprintw(y + h - 2, x + 2, "[1]Выполнена [2]Подзадача [3]Ред. [4]Удалить");
        attroff(COLOR_PAIR(CP_LOW));
    }

    void handleDetail(int ch) {
        switch (ch) {
            case '1': {
                Task* t = tm.findById(detailTaskId);
                if (t) { t->lastDone = time(nullptr); showMessage("Выполнено!", SCR_DETAIL); }
                break;
            }
            case '2':
                enterForm(false, detailTaskId);
                break;
            case '3':
                enterForm(true, detailTaskId);
                break;
            case '4': {
                Task* t = tm.findById(detailTaskId);
                if (t) {
                    int subs = tm.countSubtasks(*t);
                    string txt = "Удалить \"" + t->name + "\"?";
                    if (subs > 0) txt += " (" + toStr(subs) + " подзадач)";
                    askConfirm("Удаление", txt, [this]() {
                        tm.removeById(detailTaskId);
                        showMessage("Удалено.", SCR_LIST);
                    });
                }
                break;
            }
            case 27: // Esc
                enterList();
                break;
        }
    }

    // ===================== FORM =====================
    void enterForm(bool edit, int taskOrParentId) {
        formEdit = edit;
        formField = 0;
        screen = SCR_FORM;
        if (edit) {
            Task* t = tm.findById(taskOrParentId);
            if (!t) { screen = SCR_LIST; return; }
            f_name = t->name;
            f_desc = t->description;
            f_date = t->hasDueDate ? formatDate(t->dueDate) : "";
            f_hasDue = t->hasDueDate;
            f_diff = t->difficulty;
            f_urg = t->urgency;
            formParentId = -2; // marker: editing
            detailTaskId = taskOrParentId;
        } else {
            f_name.clear();
            f_desc.clear();
            f_date.clear();
            f_hasDue = false;
            f_diff = DIFF_HIGH;
            f_urg = URG_LOW;
            formParentId = taskOrParentId;
        }
        curs_set(1);
    }

    void drawForm() {
        int h = 16;
        int w = min(cols - 4, 64);
        int y = (rows - h) / 2;
        int x = (cols - w) / 2;
        drawBox(y, x, h, w);

        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        string title = formEdit ? " РЕДАКТИРОВАНИЕ " : " НОВАЯ ЗАДАЧА ";
        mvprintw(y, x + (w - title.size()) / 2, "%s", title.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

        const char* labels[FORM_FIELDS] = {
            "Название:  ",
            "Описание:  ",
            "Дата(ДД.ММ.ГГГГ ЧЧ:ММ): ",
            "Есть срок: ",
            "Сложность: ",
            "Срочность: "
        };
        string values[FORM_FIELDS] = {
            f_name,
            f_desc,
            f_date,
            f_hasDue ? "Да" : "Нет",
            diffName(f_diff),
            urgName(f_urg)
        };

        for (int i = 0; i < FORM_FIELDS; ++i) {
            int ly = y + 2 + i;
            bool active = (i == formField);
            if (active) attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attron(COLOR_PAIR(CP_SECONDARY));
            mvprintw(ly, x + 2, "%s", labels[i]);
            if (active) attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attroff(COLOR_PAIR(CP_SECONDARY));

            int vx = x + 2 + strlen(labels[i]);
            if (active) attron(COLOR_PAIR(CP_SELECTED));
            else attron(COLOR_PAIR(CP_NORMAL));
            // Draw field background
            int fw = w - (vx - x) - 4;
            if (fw < 10) fw = 10;
            string display = values[i];
            if ((int)display.size() > fw) display = display.substr(0, fw);
            mvprintw(ly, vx, "%-*s", fw, display.c_str());
            if (active) {
                // cursor at end
                move(ly, vx + display.size());
            }
            if (active) attroff(COLOR_PAIR(CP_SELECTED));
            else attroff(COLOR_PAIR(CP_NORMAL));
        }
    }

    void handleForm(int ch) {
        if (ch == 27) { // Esc
            curs_set(0);
            if (formEdit) screen = SCR_DETAIL;
            else if (formParentId >= 0) screen = SCR_DETAIL; // was adding subtask
            else screen = SCR_LIST;
            return;
        }
        if (ch == '\t' || ch == KEY_DOWN) {
            formField = (formField + 1) % FORM_FIELDS;
            return;
        }
        if (ch == KEY_BTAB || ch == KEY_UP) {
            formField = (formField + FORM_FIELDS - 1) % FORM_FIELDS;
            return;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            if (f_name.empty()) {
                showMessage("Название обязательно!", SCR_FORM);
                return;
            }
            Task t;
            if (formEdit) {
                Task* existing = tm.findById(detailTaskId);
                if (!existing) { screen = SCR_LIST; return; }
                t = *existing; // copy
            } else {
                t.id = tm.nextId++;
                t.lastDone = time(nullptr);
            }
            t.name = f_name;
            t.description = f_desc;
            t.hasDueDate = f_hasDue;
            if (f_hasDue && !f_date.empty()) {
                if (!parseDate(f_date, t.dueDate)) {
                    showMessage("Неверный формат даты! Используйте ДД.ММ.ГГГГ ЧЧ:ММ", SCR_FORM);
                    return;
                }
            } else {
                t.hasDueDate = false;
            }
            t.difficulty = f_diff;
            t.urgency = f_urg;

            if (formEdit) {
                *tm.findById(detailTaskId) = t;
                showMessage("Сохранено!", SCR_DETAIL);
            } else {
                if (formParentId >= 0) {
                    Task* p = tm.findById(formParentId);
                    if (p) p->subtasks.push_back(t);
                    else tm.roots.push_back(t);
                } else {
                    tm.roots.push_back(t);
                }
                rebuildFlat();
                showMessage("Добавлено!", SCR_LIST);
            }
            curs_set(0);
            return;
        }

        // Field editing
        switch (formField) {
            case 0: // name
            case 1: // desc
            case 2: // date
                if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
                    string& s = (formField == 0) ? f_name : (formField == 1) ? f_desc : f_date;
                    if (!s.empty()) s.pop_back();
                } else if (ch >= 32 && ch < 127) {
                    string& s = (formField == 0) ? f_name : (formField == 1) ? f_desc : f_date;
                    if (s.size() < 100) s.push_back((char)ch);
                }
                break;
            case 3: // hasDue toggle
                if (ch == ' ' || ch == '\n' || ch == KEY_RIGHT || ch == KEY_LEFT)
                    f_hasDue = !f_hasDue;
                break;
            case 4: // diff
                if (ch == KEY_RIGHT || ch == ' ') f_diff = (f_diff + 1) % 3;
                if (ch == KEY_LEFT) f_diff = (f_diff + 2) % 3;
                break;
            case 5: // urg
                if (ch == KEY_RIGHT || ch == ' ') f_urg = (f_urg + 1) % 3;
                if (ch == KEY_LEFT) f_urg = (f_urg + 2) % 3;
                break;
        }
    }

    // ===================== KANBAN =====================
    void enterKanban(int mode) {
        kanbanMode = mode;
        kanbanCol = 0;
        kanbanRow[0] = kanbanRow[1] = kanbanRow[2] = 0;
        screen = SCR_KANBAN;
        rebuildKanban();
    }

    void rebuildKanban() {
        for (int i = 0; i < 3; ++i) kanbanItems[i].clear();
        if (kanbanMode == 0) {
            for (int d = 0; d < 3; ++d) tm.collectByDiff(d, kanbanItems[d], tm.roots);
        } else {
            for (int u = 0; u < 3; ++u) tm.collectByUrg(u, kanbanItems[u], tm.roots);
        }
    }

    void drawKanban() {
        attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
        string title = kanbanMode == 0 ? "К А Н Б А Н :  С Л О Ж Н О С Т Ь" : "К А Н Б А Н :  С Р О Ч Н О С Т Ь";
        mvprintw(0, (cols - title.size()) / 2, "%s", title.c_str());
        attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

        const char* headers[3] = {
            kanbanMode == 0 ? " ВЫСОКАЯ " : " ВЫСОКАЯ ",
            kanbanMode == 0 ? " СРЕДНЯЯ  " : " СРЕДНЯЯ  ",
            kanbanMode == 0 ? " НИЗКАЯ   " : " НИЗКАЯ   "
        };

        int colW = (cols - 4) / 3;
        int startY = 2;
        int contentH = rows - 4;

        for (int c = 0; c < 3; ++c) {
            int cx = 1 + c * (colW + 1);
            // Header
            attron(COLOR_PAIR(CP_HEADER) | A_BOLD | A_UNDERLINE);
            int hx = cx + (colW - strlen(headers[c])) / 2;
            if (c == kanbanCol) attron(A_REVERSE);
            mvprintw(startY, max(cx, hx), "%s", headers[c]);
            if (c == kanbanCol) attroff(A_REVERSE);
            attroff(COLOR_PAIR(CP_HEADER) | A_BOLD | A_UNDERLINE);

            // Border
            attron(COLOR_PAIR(CP_BORDER));
            mvhline(startY + 1, cx, ACS_HLINE, colW);
            mvvline(startY + 2, cx - 1, ACS_VLINE, contentH - 1);
            if (c == 2) mvvline(startY + 2, cx + colW, ACS_VLINE, contentH - 1);
            attroff(COLOR_PAIR(CP_BORDER));

            // Items
            int visibleItems = contentH - 2;
            auto& items = kanbanItems[c];
            if (kanbanRow[c] >= (int)items.size()) kanbanRow[c] = max(0, (int)items.size() - 1);
            int scroll = 0;
            if (kanbanRow[c] >= visibleItems) scroll = kanbanRow[c] - visibleItems + 1;

            for (int i = 0; i < visibleItems && scroll + i < (int)items.size(); ++i) {
                int idx = scroll + i;
                int y = startY + 2 + i;
                bool sel = (c == kanbanCol && idx == kanbanRow[c]);
                if (sel) attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
                else attron(COLOR_PAIR(CP_NORMAL));

                string txt = items[idx]->name;
                string ago = " [" + formatAgo(items[idx]->lastDone) + "]";
                int maxLen = colW - 2;
                if ((int)(txt + ago).size() > maxLen) {
                    if ((int)txt.size() > maxLen - 4) txt = txt.substr(0, maxLen - 4) + "..";
                }
                string line = txt + ago;
                if ((int)line.size() > maxLen) line = line.substr(0, maxLen);
                mvprintw(y, cx + 1, "%-*s", maxLen, line.c_str());

                if (sel) attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
                else attroff(COLOR_PAIR(CP_NORMAL));
            }
        }
    }

    void handleKanban(int ch) {
        switch (ch) {
            case KEY_LEFT: case 'h':
                kanbanCol = (kanbanCol + 2) % 3;
                break;
            case KEY_RIGHT: case 'l':
                kanbanCol = (kanbanCol + 1) % 3;
                break;
            case KEY_UP: case 'k':
                if (kanbanRow[kanbanCol] > 0) kanbanRow[kanbanCol]--;
                break;
            case KEY_DOWN: case 'j':
                if (kanbanRow[kanbanCol] < (int)kanbanItems[kanbanCol].size() - 1)
                    kanbanRow[kanbanCol]++;
                break;
            case '\t':
                enterKanban(kanbanMode == 0 ? 1 : 0);
                break;
            case '\n': case KEY_ENTER:
                if (kanbanRow[kanbanCol] < (int)kanbanItems[kanbanCol].size()) {
                    enterDetail(kanbanItems[kanbanCol][kanbanRow[kanbanCol]]->id);
                }
                break;
            case 27:
                screen = SCR_MENU;
                break;
        }
    }

    // ===================== CONFIRM =====================
    void askConfirm(const string& title, const string& text, function<void()> yesAction) {
        confirmTitle = title;
        confirmText = text;
        confirmYes = yesAction;
        confirmSel = 0;
        msgNextScreen = screen;
        screen = SCR_CONFIRM;
    }

    void drawConfirm() {
        int h = 8;
        int tw = confirmText.size();
        int w = max(tw + 6, 40);
        w = min(w, cols - 4);
        int y = (rows - h) / 2;
        int x = (cols - w) / 2;
        drawCenteredWindow(h, w, confirmTitle);

        attron(COLOR_PAIR(CP_NORMAL));
        int lines = 1 + (confirmText.size() / (w - 4));
        for (int i = 0; i < lines; ++i) {
            string part = confirmText.substr(i * (w - 4), w - 4);
            mvprintw(y + 2 + i, x + 2, "%s", part.c_str());
        }
        attroff(COLOR_PAIR(CP_NORMAL));

        const char* opts[2] = {"[ Нет ]", "[ Да ]"};
        int ox[2];
        ox[0] = x + w / 2 - 10;
        ox[1] = x + w / 2 + 2;
        for (int i = 0; i < 2; ++i) {
            if (i == confirmSel) attron(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attron(COLOR_PAIR(CP_NORMAL));
            mvprintw(y + h - 2, ox[i], "%s", opts[i]);
            if (i == confirmSel) attroff(COLOR_PAIR(CP_SELECTED) | A_BOLD);
            else attroff(COLOR_PAIR(CP_NORMAL));
        }
    }

    void handleConfirm(int ch) {
        switch (ch) {
            case KEY_LEFT: confirmSel = 0; break;
            case KEY_RIGHT: confirmSel = 1; break;
            case '\n': case KEY_ENTER:
                if (confirmSel == 1 && confirmYes) confirmYes();
                else screen = msgNextScreen;
                break;
            case 27:
                screen = msgNextScreen;
                break;
        }
    }

    // ===================== MESSAGE =====================
    void showMessage(const string& text, int returnScreen) {
        msgText = text;
        msgNextScreen = returnScreen;
        screen = SCR_MSG;
    }

    void drawMessage() {
        int h = 7;
        int w = max((int)msgText.size() + 6, 30);
        w = min(w, cols - 4);
        int y = (rows - h) / 2;
        int x = (cols - w) / 2;
        drawCenteredWindow(h, w, " Сообщение ");

        attron(COLOR_PAIR(CP_NORMAL) | A_BOLD);
        int lines = 1 + (msgText.size() / (w - 4));
        for (int i = 0; i < lines; ++i) {
            string part = msgText.substr(i * (w - 4), w - 4);
            mvprintw(y + 2 + i, x + 2, "%s", part.c_str());
        }
        attroff(COLOR_PAIR(CP_NORMAL) | A_BOLD);

        attron(COLOR_PAIR(CP_SELECTED));
        string ok = "[ OK ]";
        mvprintw(y + h - 2, x + (w - ok.size()) / 2, "%s", ok.c_str());
        attroff(COLOR_PAIR(CP_SELECTED));
    }

    void handleMessage(int ch) {
        if (ch == '\n' || ch == KEY_ENTER || ch == 27 || ch == ' ') {
            screen = msgNextScreen;
        }
    }
};

// ======================== MAIN ========================
int main() {
    // Установка локали UTF-8
    setenv("LANG", "en_US.UTF-8", 1);
    setenv("LC_ALL", "en_US.UTF-8", 1);
    setlocale(LC_ALL, "C.UTF-8");
    setlocale(LC_CTYPE, "UTF-8");
    printf("\033%%G");  // включить UTF-8 в некоторых терминалах
    fflush(stdout);
    const char* locale = setlocale(LC_ALL, NULL);
mvprintw(0, 0, "Текущая локаль: %s", locale ? locale : "не установлена");
refresh();
getch();
while(1);
    Application app;
    app.run();
    return 0;
}