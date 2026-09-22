#include <iostream>
#include <vector>
#include <string>
#include <ctime>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>

using namespace std;

// ===================== ПЕРЕЧИСЛЕНИЯ =====================
enum Difficulty { DIFF_HIGH = 0, DIFF_MEDIUM = 1, DIFF_LOW = 2 };
enum Urgency    { URG_HIGH = 0,  URG_MEDIUM = 1,  URG_LOW = 2 };

string diffToStr(Difficulty d) {
    switch(d) {
        case DIFF_HIGH:   return "Высокая";
        case DIFF_MEDIUM: return "Средняя";
        case DIFF_LOW:    return "Низкая";
    }
    return "?";
}

string urgToStr(Urgency u) {
    switch(u) {
        case URG_HIGH:   return "Высокая";
        case URG_MEDIUM: return "Средняя";
        case URG_LOW:    return "Низкая";
    }
    return "?";
}

// ===================== УТИЛИТЫ =====================
void clearScreen() {
    for (int i = 0; i < 40; i++) cout << "\n";
}

void waitEnter() {
    cout << "\nНажмите Enter...";
    cin.get();
}

int getInt(int vmin, int vmax) {
    int v;
    while (true) {
        if (cin >> v) {
            if (v >= vmin && v <= vmax) {
                cin.ignore(10000, '\n');
                return v;
            }
        }
        cin.clear();
        cin.ignore(10000, '\n');
        cout << "Введите число от " << vmin << " до " << vmax << ": ";
    }
}

string getLine() {
    string s;
    getline(cin, s);
    return s;
}

int toInt(const string& s) {
    stringstream ss(s);
    int x = 0;
    ss >> x;
    return x;
}

long long toLL(const string& s) {
    stringstream ss(s);
    long long x = 0;
    ss >> x;
    return x;
}

// ===================== ЗАДАЧА =====================
struct Task {
    int id;
    string name;
    string description;
    time_t lastDone;
    time_t dueDate;
    bool hasDueDate;
    Difficulty difficulty;
    Urgency urgency;
    vector<Task> subtasks;
    bool expanded;

    Task() : id(0), lastDone(time(0)), dueDate(0), hasDueDate(false),
             difficulty(DIFF_HIGH), urgency(URG_LOW), expanded(true) {}

    string formatAgo() const {
        time_t now = time(0);
        double diff = difftime(now, lastDone);
        if (diff < 0) diff = 0;
        int hours = (int)(diff / 3600.0);
        int days  = hours / 24;
        int months = days / 30;
        int years = days / 365;

        if (diff < 86400) {
            return string(hours == 0 ? "<1ч" : toStr(hours) + "ч");
        } else if (diff < 2592000) {
            return toStr(days) + "д " + toStr(hours % 24) + "ч";
        } else if (diff < 31536000) {
            return toStr(months) + "м " + toStr(days % 30) + "д " + toStr(hours % 24) + "ч";
        } else {
            return toStr(years) + "г " + toStr(months % 12) + "м " + toStr(days % 30) + "д " + toStr(hours % 24) + "ч";
        }
    }

    string formatDue() const {
        if (!hasDueDate) return "-";
        tm* ti = localtime(&dueDate);
        char buf[32];
        strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", ti);
        return string(buf);
    }

    static string toStr(int x) {
        stringstream ss;
        ss << x;
        return ss.str();
    }

    void printTree(int level, int& num, vector< pair<int, Task*> >& flat) {
        for (int i = 0; i < level; i++) cout << "  ";
        cout << (expanded ? "[-] " : "[+]") << " " << num << ". " << name
             << "  [" << formatAgo() << "]"
             << "  C:" << diffToStr(difficulty)
             << "  Cr:" << urgToStr(urgency);
        if (hasDueDate) cout << "  -> " << formatDue();
        cout << "\n";
        flat.push_back(make_pair(num, this));
        num++;
        if (expanded) {
            for (size_t i = 0; i < subtasks.size(); i++) {
                subtasks[i].printTree(level + 1, num, flat);
            }
        }
    }

    void save(ofstream& out) const {
        out << "BEGIN\n";
        out << id << "\n";
        out << name << "\n";
        out << description << "\n";
        out << (long long)lastDone << "\n";
        out << (long long)dueDate << "\n";
        out << (hasDueDate ? 1 : 0) << "\n";
        out << difficulty << "\n";
        out << urgency << "\n";
        out << subtasks.size() << "\n";
        for (size_t i = 0; i < subtasks.size(); i++)
            subtasks[i].save(out);
        out << "END\n";
    }

    void load(ifstream& in) {
        string line;
        getline(in, line); // BEGIN
        getline(in, line); id = toInt(line);
        getline(in, name);
        getline(in, description);
        getline(in, line); lastDone = (time_t)toLL(line);
        getline(in, line); dueDate = (time_t)toLL(line);
        getline(in, line); hasDueDate = (toInt(line) != 0);
        getline(in, line); difficulty = (Difficulty)toInt(line);
        getline(in, line); urgency = (Urgency)toInt(line);
        getline(in, line); int cnt = toInt(line);
        subtasks.clear();
        for (int i = 0; i < cnt; i++) {
            Task t;
            t.load(in);
            subtasks.push_back(t);
        }
        getline(in, line); // END
    }
};

// ===================== ГЛОБАЛЬНЫЕ ДАННЫЕ =====================
vector<Task> rootTasks;
int nextId = 1;
const char* FILENAME = "repeated_tasks.dat";

// ===================== ПОИСК / СБОР =====================
void collectAll(vector<Task*>& out, vector<Task>& src) {
    for (size_t i = 0; i < src.size(); i++) {
        out.push_back(&src[i]);
        collectAll(out, src[i].subtasks);
    }
}

Task* findById(int id, vector<Task>& tasks) {
    for (size_t i = 0; i < tasks.size(); i++) {
        if (tasks[i].id == id) return &tasks[i];
        Task* p = findById(id, tasks[i].subtasks);
        if (p) return p;
    }
    return 0;
}

bool removeById(int id, vector<Task>& tasks) {
    for (size_t i = 0; i < tasks.size(); i++) {
        if (tasks[i].id == id) {
            tasks.erase(tasks.begin() + i);
            return true;
        }
        if (removeById(id, tasks[i].subtasks)) return true;
    }
    return false;
}

// ===================== КАНБАН-ДОСКИ =====================
void showKanbanDifficulty() {
    clearScreen();
    cout << "+--------------------------------------------------------------------+\n";
    cout << "|              К А Н Б А Н   Д О С К А   (С Л О Ж Н О С Т Ь)         |\n";
    cout << "+------------------------+------------------------+--------------------+\n";
    cout << "|      ВЫСОКАЯ           |       СРЕДНЯЯ          |      НИЗКАЯ        |\n";
    cout << "+------------------------+------------------------+--------------------+\n";

    vector<Task*> all;
    collectAll(all, rootTasks);
    vector<Task*> h, m, l;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i]->difficulty == DIFF_HIGH) h.push_back(all[i]);
        else if (all[i]->difficulty == DIFF_MEDIUM) m.push_back(all[i]);
        else l.push_back(all[i]);
    }
    size_t rows = h.size();
    if (m.size() > rows) rows = m.size();
    if (l.size() > rows) rows = l.size();

    for (size_t r = 0; r < rows; r++) {
        string sh = (r < h.size()) ? (h[r]->name + " [" + h[r]->formatAgo() + "]") : "";
        string sm = (r < m.size()) ? (m[r]->name + " [" + m[r]->formatAgo() + "]") : "";
        string sl = (r < l.size()) ? (l[r]->name + " [" + l[r]->formatAgo() + "]") : "";
        if (sh.length() > 22) sh = sh.substr(0, 22);
        if (sm.length() > 22) sm = sm.substr(0, 22);
        if (sl.length() > 18) sl = sl.substr(0, 18);
        cout << "| " << left << setw(22) << sh << " | " << setw(22) << sm << " | " << setw(18) << sl << " |\n";
    }
    cout << "+------------------------+------------------------+--------------------+\n";
    waitEnter();
}

void showKanbanUrgency() {
    clearScreen();
    cout << "+--------------------------------------------------------------------+\n";
    cout << "|              К А Н Б А Н   Д О С К А   (С Р О Ч Н О С Т Ь)         |\n";
    cout << "+------------------------+------------------------+--------------------+\n";
    cout << "|      ВЫСОКАЯ           |       СРЕДНЯЯ          |      НИЗКАЯ        |\n";
    cout << "+------------------------+------------------------+--------------------+\n";

    vector<Task*> all;
    collectAll(all, rootTasks);
    vector<Task*> h, m, l;
    for (size_t i = 0; i < all.size(); i++) {
        if (all[i]->urgency == URG_HIGH) h.push_back(all[i]);
        else if (all[i]->urgency == URG_MEDIUM) m.push_back(all[i]);
        else l.push_back(all[i]);
    }
    size_t rows = h.size();
    if (m.size() > rows) rows = m.size();
    if (l.size() > rows) rows = l.size();

    for (size_t r = 0; r < rows; r++) {
        string sh = (r < h.size()) ? (h[r]->name + " [" + h[r]->formatAgo() + "]") : "";
        string sm = (r < m.size()) ? (m[r]->name + " [" + m[r]->formatAgo() + "]") : "";
        string sl = (r < l.size()) ? (l[r]->name + " [" + l[r]->formatAgo() + "]") : "";
        if (sh.length() > 22) sh = sh.substr(0, 22);
        if (sm.length() > 22) sm = sm.substr(0, 22);
        if (sl.length() > 18) sl = sl.substr(0, 18);
        cout << "| " << left << setw(22) << sh << " | " << setw(22) << sm << " | " << setw(18) << sl << " |\n";
    }
    cout << "+------------------------+------------------------+--------------------+\n";
    waitEnter();
}

// ===================== РАБОТА С ЗАДАЧАМИ =====================
void addTask(Task* parent) {
    clearScreen();
    cout << "========== НОВАЯ ЗАДАЧА ==========\n";
    Task t;
    t.id = nextId++;

    cout << "Название: ";
    t.name = getLine();
    if (t.name.empty()) {
        cout << "Отмена.\n";
        waitEnter();
        return;
    }

    cout << "Описание (Enter - пропустить): ";
    t.description = getLine();

    cout << "Задать дату выполнения? (1-Да, 0-Нет, по умолчанию 0): ";
    if (getInt(0, 1) == 1) {
        cout << "Год (например 2026): ";   int y = getInt(1900, 2100);
        cout << "Месяц (1-12): ";           int mon = getInt(1, 12);
        cout << "День (1-31): ";            int d = getInt(1, 31);
        cout << "Час (0-23): ";             int h = getInt(0, 23);
        cout << "Минута (0-59): ";          int mn = getInt(0, 59);
        tm due = {};
        due.tm_year = y - 1900;
        due.tm_mon  = mon - 1;
        due.tm_mday = d;
        due.tm_hour = h;
        due.tm_min  = mn;
        due.tm_isdst = -1;
        time_t res = mktime(&due);
        if (res != -1) {
            t.dueDate = res;
            t.hasDueDate = true;
        } else {
            cout << "Некорректная дата, пропускаю.\n";
        }
    }

    cout << "Сложность (0-Высокая, 1-Средняя, 2-Низкая) [0]: ";
    t.difficulty = (Difficulty)getInt(0, 2);

    cout << "Срочность (0-Высокая, 1-Средняя, 2-Низкая) [2]: ";
    t.urgency = (Urgency)getInt(0, 2);

    if (parent) {
        parent->subtasks.push_back(t);
        cout << "\nПодзадача добавлена! ID=" << t.id << "\n";
    } else {
        rootTasks.push_back(t);
        cout << "\nЗадача добавлена! ID=" << t.id << "\n";
    }
    waitEnter();
}

void editTask(Task* t) {
    clearScreen();
    cout << "========== РЕДАКТИРОВАНИЕ ==========\n";
    cout << "Текущее название: " << t->name << "\nНовое (Enter - не менять): ";
    string s = getLine();
    if (!s.empty()) t->name = s;

    cout << "Текущее описание: " << (t->description.empty() ? "(пусто)" : t->description) << "\nНовое (Enter - не менять): ";
    s = getLine();
    if (!s.empty()) t->description = s;

    cout << "Сложность сейчас: " << diffToStr(t->difficulty) << "\nНовая (0-Выс,1-Сред,2-Низ,-1-не менять): ";
    int d = getInt(-1, 2);
    if (d >= 0) t->difficulty = (Difficulty)d;

    cout << "Срочность сейчас: " << urgToStr(t->urgency) << "\nНовая (0-Выс,1-Сред,2-Низ,-1-не менять): ";
    d = getInt(-1, 2);
    if (d >= 0) t->urgency = (Urgency)d;

    cout << "\nСохранено.\n";
    waitEnter();
}

void taskDetails(Task* t) {
    while (true) {
        clearScreen();
        cout << "========== ЗАДАЧА ==========\n";
        cout << "Название:      " << t->name << "\n";
        cout << "Описание:      " << (t->description.empty() ? "-" : t->description) << "\n";
        cout << "Послед.выполн: " << t->formatAgo() << " назад\n";
        cout << "Срок:          " << t->formatDue() << "\n";
        cout << "Сложность:     " << diffToStr(t->difficulty) << "\n";
        cout << "Срочность:     " << urgToStr(t->urgency) << "\n";
        cout << "Подзадач:      " << t->subtasks.size() << "\n";
        cout << "\n1. Отметить выполненной (обновить время)\n";
        cout << "2. Добавить подзадачу\n";
        cout << "3. Редактировать\n";
        cout << "4. Удалить эту задачу\n";
        cout << "0. Назад\n";
        cout << "Выбор: ";
        int c = getInt(0, 4);
        if (c == 0) return;
        if (c == 1) {
            t->lastDone = time(0);
            cout << "\nВремя последнего выполнения обновлено!\n";
            waitEnter();
        }
        else if (c == 2) addTask(t);
        else if (c == 3) editTask(t);
        else if (c == 4) {
            cout << "\nТочно удалить? (1-Да, 0-Нет): ";
            if (getInt(0, 1) == 1) {
                if (removeById(t->id, rootTasks)) {
                    cout << "Удалено.\n";
                    waitEnter();
                    return;
                }
            }
        }
    }
}

void showTaskList() {
    while (true) {
        clearScreen();
        cout << "========== СПИСОК ЗАДАЧ ==========\n";
        vector< pair<int, Task*> > flat;
        int num = 1;
        for (size_t i = 0; i < rootTasks.size(); i++) {
            rootTasks[i].printTree(0, num, flat);
        }
        if (flat.empty()) {
            cout << "(задач пока нет)\n";
            waitEnter();
            return;
        }
        cout << "\nВведите номер задачи для открытия,\n";
        cout << "или -N чтобы свернуть/развернуть ветку, 0-назад: ";
        int choice;
        if (!(cin >> choice)) {
            cin.clear();
            cin.ignore(10000, '\n');
            continue;
        }
        cin.ignore(10000, '\n');
        if (choice == 0) return;
        if (choice < 0) {
            Task* target = 0;
            for (size_t i = 0; i < flat.size(); i++) {
                if (flat[i].first == -choice) { target = flat[i].second; break; }
            }
            if (target) target->expanded = !target->expanded;
            continue;
        }
        Task* target = 0;
        for (size_t i = 0; i < flat.size(); i++) {
            if (flat[i].first == choice) { target = flat[i].second; break; }
        }
        if (target) taskDetails(target);
    }
}

// ===================== СОХРАНЕНИЕ / ЗАГРУЗКА =====================
void saveData() {
    ofstream out(FILENAME);
    if (!out) {
        cout << "Ошибка записи файла!\n";
        waitEnter();
        return;
    }
    out << nextId << "\n";
    out << rootTasks.size() << "\n";
    for (size_t i = 0; i < rootTasks.size(); i++)
        rootTasks[i].save(out);
    out.close();
    cout << "Сохранено в " << FILENAME << "\n";
    waitEnter();
}

void loadData() {
    ifstream in(FILENAME);
    if (!in) {
        // файл еще не создан - нормально
        return;
    }
    string line;
    getline(in, line); nextId = toInt(line);
    getline(in, line); int cnt = toInt(line);
    rootTasks.clear();
    for (int i = 0; i < cnt; i++) {
        Task t;
        t.load(in);
        rootTasks.push_back(t);
    }
    in.close();
}

// ===================== ГЛАВНОЕ МЕНЮ =====================
int main() {
    loadData();

    while (true) {
        clearScreen();
        cout << "+========================================+\n";
        cout << "|          R e p e a t e d T a s k s     |\n";
        cout << "+========================================+\n";
        cout << "|  1. Список задач (дерево)              |\n";
        cout << "|  2. Добавить корневую задачу           |\n";
        cout << "|  3. Канбан-доска: СЛОЖНОСТЬ            |\n";
        cout << "|  4. Канбан-доска: СРОЧНОСТЬ            |\n";
        cout << "|  5. Сохранить на диск                  |\n";
        cout << "|  6. Перезагрузить с диска              |\n";
        cout << "|  0. Выход (с сохранением)              |\n";
        cout << "+========================================+\n";
        cout << "Корневых задач: " << rootTasks.size() << "\n";
        cout << "Выбор: ";

        int choice = getInt(0, 6);
        switch (choice) {
            case 1: showTaskList(); break;
            case 2: addTask(0); break;
            case 3: showKanbanDifficulty(); break;
            case 4: showKanbanUrgency(); break;
            case 5: saveData(); break;
            case 6: loadData(); break;
            case 0: saveData(); return 0;
        }
    }
}
