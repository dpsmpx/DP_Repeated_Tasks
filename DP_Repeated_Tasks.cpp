// ============================================================================
//  DP_Repeated_Tasks — трекер времени, прошедшего с последнего выполнения задач.
//
//  Это НЕ планировщик: у задач намеренно нет расписания и периодичности.
//  Единственная метрика — «сколько прошло с тех пор, как это делали последний раз».
//
//  Сборка: C++98, один файл, без платформенно-зависимых вызовов (C4Droid / APK).
//  Данные: repeated_tasks.dat в рабочем каталоге = внутреннее хранилище приложения.
//          Путь намеренно относительный — так его видит C4Droid. Не менять.
// ============================================================================

#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <ctime>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

using namespace std;

// ===================== ОГРАНИЧЕНИЯ (защита от битого файла) =====================
static const int       MAX_DEPTH    = 64;          // глубина вложенности
static const int       MAX_TASKS    = 50000;       // всего задач в файле
static const int       MAX_CHILDREN = 10000;       // подзадач у одной задачи
static const long long MIN_TS       = 0LL;         // 1970-01-01
static const long long MAX_TS       = 4102444800LL;// 2100-01-01

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

// Значения из файла могут быть любыми — приводим к допустимому диапазону.
static Difficulty toDifficulty(int v) {
    if (v == DIFF_MEDIUM) return DIFF_MEDIUM;
    if (v == DIFF_LOW)    return DIFF_LOW;
    return DIFF_HIGH;
}
static Urgency toUrgency(int v) {
    if (v == URG_HIGH)   return URG_HIGH;
    if (v == URG_MEDIUM) return URG_MEDIUM;
    return URG_LOW;
}

// ===================== UTF-8 =====================
// std::string здесь хранит UTF-8, где кириллица занимает 2 байта на символ.
// Поэтому length()/substr()/setw() считают НЕ то, что видит пользователь:
// без этих помощников таблицы разъезжаются, а substr() рвёт символ пополам.
// Считаем кодовые точки — для кириллицы и латиницы это и есть ширина в ячейках.

static size_t utf8Len(const string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++n;
    return n;
}

// Обрезает по границе символа, а не байта.
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

// Обрезает до width и дополняет пробелами до ровно width видимых символов.
static string utf8Pad(const string& s, size_t width) {
    string r = utf8Trunc(s, width);
    size_t len = utf8Len(r);
    if (len < width) r.append(width - len, ' ');
    return r;
}

static string utf8Center(const string& s, size_t width) {
    string r = utf8Trunc(s, width);
    size_t len = utf8Len(r);
    if (len >= width) return r;
    size_t left = (width - len) / 2;
    return string(left, ' ') + r + string(width - len - left, ' ');
}

// ===================== ВВОД =====================
// Всё чтение — построчное. Смешивать operator>> и getline нельзя: остаётся
// «висячий» перевод строки. Плюс operator>> не умеет «Enter = значение по умолчанию».

static bool g_inputClosed = false;   // stdin закрыт (Ctrl+D, конвейер, закрытый терминал)

static string trimStr(const string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t')) ++b;
    while (e > b && (s[e-1] == ' ' || s[e-1] == '\t')) --e;
    return s.substr(b, e - b);
}

// Единственная точка чтения. Возвращает false, когда ввод кончился.
// Без этой проверки cin.clear() снимает eofbit, ignore() тут же выставляет его
// обратно, и цикл крутится вечно, заливая экран приглашениями.
static bool readLine(string& out) {
    if (g_inputClosed) { out.clear(); return false; }
    if (!getline(cin, out)) {
        g_inputClosed = true;
        out.clear();
        return false;
    }
    // Файл мог быть отредактирован на ПК — убираем CR от CRLF.
    if (!out.empty() && out[out.size()-1] == '\r') out.erase(out.size()-1);
    return true;
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
        if (v > (9223372036854775807LL - (s[i] - '0')) / 10) return false;  // переполнение
        v = v * 10 + (s[i] - '0');
    }
    out = neg ? -v : v;
    return true;
}

// hasDef == true: пустая строка (просто Enter) возвращает defVal.
// При закрытом вводе возвращает defVal/vmin и больше не спрашивает.
static int getInt(int vmin, int vmax, bool hasDef, int defVal) {
    string s;
    while (true) {
        if (!readLine(s)) return hasDef ? defVal : vmin;
        s = trimStr(s);
        if (s.empty() && hasDef) return defVal;
        int v;
        if (!s.empty() && parseInt(s, v) && v >= vmin && v <= vmax) return v;
        cout << "Введите число от " << vmin << " до " << vmax;
        if (hasDef) cout << " (Enter = " << defVal << ")";
        cout << ": ";
    }
}

static string getLine() {
    string s;
    readLine(s);
    return s;
}

static void waitEnter() {
    if (g_inputClosed) return;
    cout << "\nНажмите Enter...";
    string s;
    readLine(s);
}

static bool confirm(const string& question, bool defYes) {
    cout << question << (defYes ? " (1-Да, 0-Нет) [1]: " : " (1-Да, 0-Нет) [0]: ");
    return getInt(0, 1, true, defYes ? 1 : 0) == 1;
}

static void clearScreen() {
    // Намеренно переводами строки, а не ANSI-escape: терминал C4Droid
    // не гарантирует поддержку escape-последовательностей.
    for (int i = 0; i < 40; ++i) cout << "\n";
}

static string intToStr(long long x) {
    stringstream ss;
    ss << x;
    return ss.str();
}

// В построчном формате перевод строки внутри поля сломал бы разбор.
static string sanitizeField(const string& s) {
    string r = s;
    for (size_t i = 0; i < r.size(); ++i)
        if (r[i] == '\n' || r[i] == '\r') r[i] = ' ';
    return r;
}

// ===================== ЗАДАЧА =====================
struct Task {
    int         id;
    string      name;
    string      description;
    time_t      lastDone;
    bool        neverDone;    // задачу ещё ни разу не отмечали выполненной
    time_t      dueDate;      // необязательная пометка «не позже», не расписание
    bool        hasDueDate;
    Difficulty  difficulty;
    Urgency     urgency;
    vector<Task> subtasks;
    bool        expanded;

    Task()
        : id(0), lastDone(time(0)), neverDone(true), dueDate(0), hasDueDate(false),
          difficulty(DIFF_HIGH), urgency(URG_LOW), expanded(true) {}

    // Главная метрика приложения. Раскладываем последовательным вычитанием:
    // прежняя версия считала years = days/365 и months = days/30 независимо
    // от одной величины, а остаток брала как days%30. Базы несовместимы
    // (12*30 = 360 != 365), поэтому день «уезжал» на 5 суток за каждый год.
    // Год здесь = 365 дней, месяц = 30 дней; приближение грубое, но согласованное.
    string formatAgo() const {
        if (neverDone) return "никогда";

        double diff = difftime(time(0), lastDone);
        if (diff < 0) diff = 0;   // часы пользователя сдвинулись назад

        long long total  = static_cast<long long>(diff);
        long long hours  = (total % 86400) / 3600;
        long long days   = total / 86400;
        long long years  = days / 365;  days   -= years * 365;
        long long months = days / 30;   days   -= months * 30;

        if (total < 86400)
            return hours == 0 ? string("<1ч") : intToStr(hours) + "ч";
        if (years == 0 && months == 0)
            return intToStr(days) + "д " + intToStr(hours) + "ч";
        if (years == 0)
            return intToStr(months) + "м " + intToStr(days) + "д " + intToStr(hours) + "ч";
        return intToStr(years) + "г " + intToStr(months) + "м "
             + intToStr(days) + "д " + intToStr(hours) + "ч";
    }

    string formatDue() const {
        if (!hasDueDate) return "-";
        // localtime() возвращает NULL, если time_t не представим в struct tm.
        // Прежняя версия передавала этот NULL в strftime — SIGSEGV прямо
        // при отрисовке списка, то есть вся база становилась недоступна.
        tm* ti = localtime(&dueDate);
        if (ti == 0) return "(некорр. дата)";
        char buf[32];
        if (strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", ti) == 0) return "(некорр. дата)";
        return string(buf);
    }

    string displayName() const {
        return name.empty() ? string("(без названия)") : name;
    }

    int countSubtree() const {
        int n = 1;
        for (size_t i = 0; i < subtasks.size(); ++i) n += subtasks[i].countSubtree();
        return n;
    }

    // flat: (номер на экране -> id задачи). Храним ИД, а не Task*:
    // указатель внутрь vector<Task> протухает при любом push_back в этот вектор.
    void printTree(int level, int& num, vector< pair<int,int> >& flat) const {
        for (int i = 0; i < level; ++i) cout << "  ";
        const bool leaf = subtasks.empty();
        cout << (leaf ? "[ ] " : (expanded ? "[-] " : "[+] "))   // ровно 4 символа во всех случаях
             << num << ". " << displayName()
             << "  [" << formatAgo() << "]"
             << "  C:" << diffToStr(difficulty)
             << "  Cr:" << urgToStr(urgency);
        if (hasDueDate) cout << "  -> " << formatDue();
        cout << "\n";
        flat.push_back(make_pair(num, id));
        ++num;
        if (expanded)
            for (size_t i = 0; i < subtasks.size(); ++i)
                subtasks[i].printTree(level + 1, num, flat);
    }
};

// ===================== ГЛОБАЛЬНЫЕ ДАННЫЕ =====================
static vector<Task> rootTasks;
static int          nextId     = 1;
static bool         g_dirty    = false;   // есть несохранённые изменения

static const char* FILENAME     = "repeated_tasks.dat";
static const char* FILENAME_TMP = "repeated_tasks.dat.tmp";
static const char* FILENAME_BAK = "repeated_tasks.dat.bak";
static const char* MAGIC_V2     = "RTASKS2";

// ===================== ПОИСК / СБОР =====================
static void collectAll(vector<Task*>& out, vector<Task>& src) {
    for (size_t i = 0; i < src.size(); ++i) {
        out.push_back(&src[i]);
        collectAll(out, src[i].subtasks);
    }
}

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

// ===================== СОХРАНЕНИЕ =====================
static void writeTask(ofstream& out, const Task& t) {
    out << "BEGIN\n"
        << t.id << "\n"
        << sanitizeField(t.name) << "\n"
        << sanitizeField(t.description) << "\n"
        << static_cast<long long>(t.lastDone) << "\n"
        << (t.neverDone ? 1 : 0) << "\n"
        << static_cast<long long>(t.dueDate) << "\n"
        << (t.hasDueDate ? 1 : 0) << "\n"
        << static_cast<int>(t.difficulty) << "\n"
        << static_cast<int>(t.urgency) << "\n"
        << (t.expanded ? 1 : 0) << "\n"
        << t.subtasks.size() << "\n";
    for (size_t i = 0; i < t.subtasks.size(); ++i) writeTask(out, t.subtasks[i]);
    out << "END\n";
}

// Пишем во временный файл и подменяем им рабочий через rename().
// Прежняя версия открывала рабочий файл напрямую (ofstream сразу обрезает его
// в ноль), проверяла поток только при открытии и печатала «Сохранено» даже
// после неудачной записи. Оборванная запись уничтожала данные без следа.
// rename() в пределах одного каталога атомарен, поэтому tmp/bak лежат рядом
// с рабочим файлом — во внутреннем хранилище приложения.
static bool writeAllTo(const char* path, string& err) {
    ofstream out(path);
    if (!out) { err = "не удалось открыть файл для записи"; return false; }

    out << MAGIC_V2 << "\n" << nextId << "\n" << rootTasks.size() << "\n";
    for (size_t i = 0; i < rootTasks.size(); ++i) writeTask(out, rootTasks[i]);

    out.flush();
    if (!out.good()) { err = "ошибка записи (нет места на устройстве?)"; out.close(); return false; }
    out.close();
    if (out.fail()) { err = "ошибка при закрытии файла"; return false; }
    return true;
}

static bool saveData(bool quiet) {
    string err;
    if (!writeAllTo(FILENAME_TMP, err)) {
        remove(FILENAME_TMP);                       // рабочий файл не тронут
        cout << "ОШИБКА СОХРАНЕНИЯ: " << err << "\n";
        cout << "Данные НЕ записаны, прежний файл оставлен без изменений.\n";
        if (!quiet) waitEnter();
        return false;
    }

    remove(FILENAME_BAK);
    rename(FILENAME, FILENAME_BAK);                 // не существует — просто не сработает

    if (rename(FILENAME_TMP, FILENAME) != 0) {
        cout << "ОШИБКА: не удалось заменить файл данных.\n";
        cout << "Предыдущая версия сохранена в " << FILENAME_BAK << "\n";
        if (!quiet) waitEnter();
        return false;
    }

    g_dirty = false;
    if (!quiet) {
        cout << "Сохранено в " << FILENAME << "\n";
        waitEnter();
    }
    return true;
}

// ===================== ЗАГРУЗКА =====================
// Читаем в отдельное дерево и присваиваем его только при полном успехе:
// на битом файле прежняя версия успевала сделать rootTasks.clear() и молча
// оставляла пользователя с пустой базой.

struct Loader {
    ifstream& in;
    bool      v2;
    int       total;
    string    err;

    Loader(ifstream& i, bool isV2) : in(i), v2(isV2), total(0) {}

    bool line(string& out) {
        if (!getline(in, out)) { err = "файл оборван"; return false; }
        if (!out.empty() && out[out.size()-1] == '\r') out.erase(out.size()-1);
        return true;
    }

    bool intLine(int& out, int lo, int hi, const char* what) {
        string s;
        if (!line(s)) return false;
        if (!parseInt(trimStr(s), out)) { err = string("нечисловое поле: ") + what; return false; }
        if (out < lo || out > hi) { err = string("значение вне диапазона: ") + what; return false; }
        return true;
    }

    bool timeLine(time_t& out) {
        string s;
        long long v;
        if (!line(s)) return false;
        if (!parseLL(trimStr(s), v)) { err = "нечисловая метка времени"; return false; }
        if (v < MIN_TS) v = MIN_TS;     // чиним, а не отвергаем: данные важнее строгости
        if (v > MAX_TS) v = MAX_TS;
        out = static_cast<time_t>(v);
        return true;
    }

    bool readTask(Task& t, int depth) {
        if (depth > MAX_DEPTH)  { err = "слишком глубокая вложенность";  return false; }
        if (++total > MAX_TASKS) { err = "слишком много задач в файле";  return false; }

        string s;
        if (!line(s)) return false;
        if (trimStr(s) != "BEGIN") { err = "ожидался маркер BEGIN"; return false; }

        int v;
        if (!intLine(t.id, 0, 2147483647, "id")) return false;
        if (!line(t.name)) return false;
        if (!line(t.description)) return false;
        if (!timeLine(t.lastDone)) return false;

        if (v2) {
            if (!intLine(v, 0, 1, "neverDone")) return false;
            t.neverDone = (v != 0);
        } else {
            t.neverDone = false;   // в старом формате поля не было
        }

        if (!timeLine(t.dueDate)) return false;
        if (!intLine(v, 0, 1, "hasDueDate")) return false;
        t.hasDueDate = (v != 0);

        if (!intLine(v, -2147483647, 2147483647, "difficulty")) return false;
        t.difficulty = toDifficulty(v);
        if (!intLine(v, -2147483647, 2147483647, "urgency")) return false;
        t.urgency = toUrgency(v);

        if (v2) {
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
        if (trimStr(s) != "END") { err = "ожидался маркер END"; return false; }
        return true;
    }
};

// Уникальность id ничем не обеспечивалась, а removeById() удаляет первое
// совпадение — при совпавших id пользователь удалял одну задачу, а исчезала
// другая. Перенумеровываем дубликаты и выставляем nextId выше всех занятых.
static void reindex(vector<Task>& tasks, vector<int>& seen, int& maxId, bool& renumbered) {
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

static void collectMaxId(const vector<Task>& tasks, int& maxId) {
    for (size_t i = 0; i < tasks.size(); ++i) {
        if (tasks[i].id > maxId) maxId = tasks[i].id;
        collectMaxId(tasks[i].subtasks, maxId);
    }
}

static bool loadData(bool quiet) {
    ifstream in(FILENAME);
    if (!in) return true;   // файла ещё нет — обычный первый запуск

    string first;
    if (!getline(in, first)) {
        // Пустой файл остаётся, например, после оборванной записи.
        // Прежняя версия молча получала nextId = 0 и стирала все задачи.
        if (!quiet) {
            cout << "Файл данных пуст — возможно, сохранение было прервано.\n";
            cout << "Текущие задачи оставлены без изменений.\n";
            if (ifstream(FILENAME_BAK)) cout << "Резервная копия: " << FILENAME_BAK << "\n";
            waitEnter();
        }
        return false;
    }
    if (!first.empty() && first[first.size()-1] == '\r') first.erase(first.size()-1);

    bool v2 = (trimStr(first) == MAGIC_V2);
    int  loadedNextId = 1;

    if (v2) {
        string s;
        if (!getline(in, s) || !parseInt(trimStr(s), loadedNextId)) loadedNextId = 1;
    } else {
        if (!parseInt(trimStr(first), loadedNextId)) loadedNextId = 1;   // старый формат: id в первой строке
    }

    string s;
    int cnt = 0;
    if (!getline(in, s) || !parseInt(trimStr(s), cnt) || cnt < 0 || cnt > MAX_TASKS) {
        if (!quiet) {
            cout << "Файл данных повреждён: некорректное число корневых задач.\n";
            cout << "Текущие задачи оставлены без изменений.\n";
            waitEnter();
        }
        return false;
    }

    Loader ld(in, v2);
    vector<Task> loaded;
    loaded.reserve(static_cast<size_t>(cnt));
    for (int i = 0; i < cnt; ++i) {
        Task t;
        if (!ld.readTask(t, 1)) {
            if (!quiet) {
                cout << "Файл данных повреждён: " << ld.err << "\n";
                cout << "Прочитано задач до ошибки: " << i << " из " << cnt << "\n";
                cout << "Текущие задачи оставлены без изменений.\n";
                if (ifstream(FILENAME_BAK)) cout << "Резервная копия: " << FILENAME_BAK << "\n";
                waitEnter();
            }
            return false;
        }
        loaded.push_back(t);
    }

    // Файл разобран целиком — только теперь подменяем рабочие данные.
    rootTasks.swap(loaded);

    int maxId = 0;
    collectMaxId(rootTasks, maxId);
    vector<int> seen;
    bool renumbered = false;
    reindex(rootTasks, seen, maxId, renumbered);

    nextId = (loadedNextId > maxId ? loadedNextId : maxId + 1);
    if (nextId < 1) nextId = 1;
    g_dirty = renumbered;

    if (renumbered && !quiet) {
        cout << "В файле были повторяющиеся идентификаторы — они перенумерованы.\n";
        waitEnter();
    }
    return true;
}

// ===================== КАНБАН-ДОСКА =====================
// Раньше это были две почти одинаковые функции по 30 строк.
static void showKanban(bool byDifficulty) {
    const size_t COL = 22;

    clearScreen();
    string sep = "+" + string(COL + 2, '-') + "+" + string(COL + 2, '-')
               + "+" + string(COL + 2, '-') + "+";
    size_t inner = sep.size() - 2;

    cout << sep << "\n";
    cout << "|" << utf8Center(byDifficulty ? "К А Н Б А Н   Д О С К А   (С Л О Ж Н О С Т Ь)"
                                           : "К А Н Б А Н   Д О С К А   (С Р О Ч Н О С Т Ь)",
                              inner) << "|\n";
    cout << sep << "\n";
    cout << "| " << utf8Pad("ВЫСОКАЯ", COL) << " | " << utf8Pad("СРЕДНЯЯ", COL)
         << " | " << utf8Pad("НИЗКАЯ", COL) << " |\n";
    cout << sep << "\n";

    vector<Task*> all;
    collectAll(all, rootTasks);
    vector<Task*> col[3];
    for (size_t i = 0; i < all.size(); ++i) {
        int k = byDifficulty ? static_cast<int>(all[i]->difficulty)
                             : static_cast<int>(all[i]->urgency);
        col[k].push_back(all[i]);
    }

    size_t rows = 0;
    for (int k = 0; k < 3; ++k) if (col[k].size() > rows) rows = col[k].size();

    if (rows == 0) {
        cout << "| " << utf8Pad("(задач пока нет)", COL) << " | " << utf8Pad("", COL)
             << " | " << utf8Pad("", COL) << " |\n";
    }
    for (size_t r = 0; r < rows; ++r) {
        cout << "|";
        for (int k = 0; k < 3; ++k) {
            string cell;
            if (r < col[k].size())
                cell = col[k][r]->displayName() + " [" + col[k][r]->formatAgo() + "]";
            cout << " " << utf8Pad(cell, COL) << " |";   // по символам, а не по байтам
        }
        cout << "\n";
    }
    cout << sep << "\n";
    waitEnter();
}

// ===================== РАБОТА С ЗАДАЧАМИ =====================
static bool askDueDate(time_t& outDue) {
    cout << "Год (1970-2099): ";  int y   = getInt(1970, 2099, false, 0);
    cout << "Месяц (1-12): ";     int mon = getInt(1, 12, false, 1);
    cout << "День (1-31): ";      int d   = getInt(1, 31, false, 1);
    cout << "Час (0-23) [0]: ";   int h   = getInt(0, 23, true, 0);
    cout << "Минута (0-59) [0]: ";int mn  = getInt(0, 59, true, 0);
    if (g_inputClosed) return false;

    tm due;
    memset(&due, 0, sizeof(due));
    due.tm_year  = y - 1900;
    due.tm_mon   = mon - 1;
    due.tm_mday  = d;
    due.tm_hour  = h;
    due.tm_min   = mn;
    due.tm_isdst = -1;

    time_t res = mktime(&due);
    if (res == static_cast<time_t>(-1)) {
        cout << "Некорректная дата, пропускаю.\n";
        return false;
    }
    outDue = res;
    return true;
}

static void addTask(Task* parent) {
    clearScreen();
    cout << "========== НОВАЯ ЗАДАЧА ==========\n";
    Task t;

    cout << "Название: ";
    t.name = trimStr(getLine());
    if (g_inputClosed) return;
    if (t.name.empty()) {
        cout << "Пустое название — отмена.\n";
        waitEnter();
        return;   // id ещё не выдан, дыр в нумерации не остаётся
    }

    cout << "Описание (Enter - пропустить): ";
    t.description = getLine();

    // Задача только что создана и ещё ни разу не выполнялась.
    // Прежняя версия ставила lastDone = сейчас, и новая задача сразу
    // показывала «<1ч», то есть основная метрика стартовала с неправды.
    t.neverDone = true;
    t.lastDone  = time(0);

    if (confirm("Отметить, что задача уже выполнялась ранее?", false)) {
        cout << "Когда её выполняли последний раз?\n";
        time_t when;
        if (askDueDate(when)) {
            t.lastDone  = when;
            t.neverDone = false;
        }
    }

    if (confirm("Задать необязательную пометку \"не позже\"?", false)) {
        time_t due;
        if (askDueDate(due)) {
            t.dueDate    = due;
            t.hasDueDate = true;
        }
    }

    cout << "Сложность (0-Высокая, 1-Средняя, 2-Низкая) [0]: ";
    t.difficulty = toDifficulty(getInt(0, 2, true, 0));

    cout << "Срочность (0-Высокая, 1-Средняя, 2-Низкая) [2]: ";
    t.urgency = toUrgency(getInt(0, 2, true, 2));

    if (g_inputClosed) return;

    t.id = nextId++;
    if (parent) {
        parent->subtasks.push_back(t);
        cout << "\nПодзадача добавлена! ID=" << t.id << "\n";
    } else {
        rootTasks.push_back(t);
        cout << "\nЗадача добавлена! ID=" << t.id << "\n";
    }
    g_dirty = true;
    waitEnter();
}

static void editTask(Task* t) {
    clearScreen();
    cout << "========== РЕДАКТИРОВАНИЕ ==========\n";

    cout << "Текущее название: " << t->displayName() << "\nНовое (Enter - не менять): ";
    string s = trimStr(getLine());
    if (!s.empty()) { t->name = s; g_dirty = true; }

    cout << "Текущее описание: " << (t->description.empty() ? "(пусто)" : t->description)
         << "\nНовое (Enter - не менять): ";
    s = getLine();
    if (!s.empty()) { t->description = s; g_dirty = true; }

    cout << "Сложность сейчас: " << diffToStr(t->difficulty)
         << "\nНовая (0-Выс, 1-Сред, 2-Низ, Enter - не менять): ";
    int d = getInt(-1, 2, true, -1);
    if (d >= 0) { t->difficulty = toDifficulty(d); g_dirty = true; }

    cout << "Срочность сейчас: " << urgToStr(t->urgency)
         << "\nНовая (0-Выс, 1-Сред, 2-Низ, Enter - не менять): ";
    d = getInt(-1, 2, true, -1);
    if (d >= 0) { t->urgency = toUrgency(d); g_dirty = true; }

    // Раньше пометку «не позже» можно было задать только при создании
    // и уже никогда не изменить и не снять.
    cout << "Пометка \"не позже\" сейчас: " << t->formatDue() << "\n";
    cout << "0 - не менять, 1 - задать/изменить, 2 - снять [0]: ";
    int dd = getInt(0, 2, true, 0);
    if (dd == 1) {
        time_t due;
        if (askDueDate(due)) { t->dueDate = due; t->hasDueDate = true; g_dirty = true; }
    } else if (dd == 2) {
        t->hasDueDate = false;
        t->dueDate    = 0;
        g_dirty       = true;
    }

    if (g_inputClosed) return;
    cout << "\nСохранено в памяти (запись на диск — пункт 5 главного меню).\n";
    waitEnter();
}

// Принимает id, а не Task*: указатель протух бы сразу после удаления задачи,
// а также при любом добавлении в родительский вектор.
static void taskDetails(int taskId) {
    while (true) {
        if (g_inputClosed) return;
        Task* t = findById(taskId, rootTasks);
        if (t == 0) return;   // задачу удалили

        clearScreen();
        cout << "========== ЗАДАЧА ==========\n";
        cout << "Название:      " << t->displayName() << "\n";
        cout << "Описание:      " << (t->description.empty() ? "-" : t->description) << "\n";
        cout << "Послед.выполн: " << t->formatAgo() << (t->neverDone ? "" : " назад") << "\n";
        cout << "Не позже:      " << t->formatDue() << "\n";
        cout << "Сложность:     " << diffToStr(t->difficulty) << "\n";
        cout << "Срочность:     " << urgToStr(t->urgency) << "\n";
        cout << "Подзадач:      " << t->subtasks.size() << "\n";
        cout << "\n1. Отметить выполненной (сбросить счётчик)\n";
        cout << "2. Добавить подзадачу\n";
        cout << "3. Редактировать\n";
        cout << "4. Удалить эту задачу\n";
        cout << "0. Назад\n";
        cout << "Выбор: ";

        int c = getInt(0, 4, true, 0);
        if (g_inputClosed || c == 0) return;

        if (c == 1) {
            t->lastDone  = time(0);
            t->neverDone = false;
            g_dirty      = true;
            cout << "\nСчётчик сброшен: задача выполнена только что.\n";
            waitEnter();
        } else if (c == 2) {
            addTask(t);
        } else if (c == 3) {
            editTask(t);
        } else if (c == 4) {
            int n = t->countSubtree();
            cout << "\n";
            if (n > 1)
                cout << "ВНИМАНИЕ: вместе с задачей будут удалены все вложенные — всего "
                     << n << " шт.\n";
            if (confirm("Точно удалить?", false)) {
                if (removeById(taskId, rootTasks)) {
                    g_dirty = true;
                    cout << "Удалено задач: " << n << "\n";
                    waitEnter();
                }
                return;
            }
        }
    }
}

static void showTaskList() {
    while (true) {
        if (g_inputClosed) return;

        clearScreen();
        cout << "========== СПИСОК ЗАДАЧ ==========\n";
        vector< pair<int,int> > flat;
        int num = 1;
        for (size_t i = 0; i < rootTasks.size(); ++i)
            rootTasks[i].printTree(0, num, flat);

        if (flat.empty()) {
            cout << "(задач пока нет)\n";
            waitEnter();
            return;
        }

        cout << "\nВведите номер задачи для открытия,\n";
        cout << "или -N чтобы свернуть/развернуть ветку, 0-назад: ";
        int choice = getInt(-(num - 1), num - 1, true, 0);
        if (g_inputClosed || choice == 0) return;

        int want = choice < 0 ? -choice : choice;
        int id   = -1;
        for (size_t i = 0; i < flat.size(); ++i)
            if (flat[i].first == want) { id = flat[i].second; break; }
        if (id < 0) continue;

        if (choice < 0) {
            Task* target = findById(id, rootTasks);
            // Сворачивать лист бессмысленно — у него нечего прятать.
            if (target && !target->subtasks.empty()) target->expanded = !target->expanded;
        } else {
            taskDetails(id);
        }
    }
}

// ===================== ГЛАВНОЕ МЕНЮ =====================
int main() {
    // Процесс могли убить сигналом посреди записи (Android закрывает приложение
    // в фоне). Тогда остаётся временный файл — рабочий при этом цел, но мусор
    // занимает место во внутреннем хранилище.
    remove(FILENAME_TMP);

    loadData(false);

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
        cout << "Корневых задач: " << rootTasks.size()
             << (g_dirty ? "   [есть несохранённые изменения]" : "") << "\n";
        cout << "Выбор: ";

        int choice = getInt(0, 6, true, 0);

        // Ввод кончился (Ctrl+D, конвейер, закрытый терминал). Прежняя версия
        // здесь зацикливалась навсегда: cin.clear() снимал eofbit, а ignore()
        // тут же выставлял его обратно.
        if (g_inputClosed) {
            cout << "\nВвод закрыт — сохраняю и выхожу.\n";
            saveData(true);
            return 0;
        }

        switch (choice) {
            case 1: showTaskList(); break;
            case 2: addTask(0); break;
            case 3: showKanban(true); break;
            case 4: showKanban(false); break;
            case 5: saveData(false); break;
            case 6:
                // Перезагрузка затирает всё, что не записано на диск.
                if (g_dirty && !confirm("\nЕсть несохранённые изменения. Они будут потеряны. Продолжить?", false))
                    break;
                if (loadData(false))
                    cout << "Данные перечитаны с диска.\n";
                waitEnter();
                break;
            case 0:
                if (!saveData(true)) {
                    cout << "\nВНИМАНИЕ: сохранить не удалось.\n";
                    if (!confirm("Выйти всё равно (изменения будут потеряны)?", false))
                        break;
                }
                cout << "\nДо встречи!\n";
                return 0;
        }
    }
}
