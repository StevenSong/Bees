#include <unistd.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <vector>
#include <iostream>
#include <string>
#include <cstring>
#include <signal.h>

#define READ  0
#define WRITE 1
#define ERROR -1
#define COME_HOME "come home bee"
#define AVAILABLE "bee need task"
#define POISON "ahh a bee eater!"
#define BUF 200

using namespace std;

bool reallybadequal(char *a, char *b)
{
    int a_len = strlen(a);
    int b_len = strlen(b);

    int min = a_len < b_len ? a_len : b_len;

    for (int i = 0; i < min; i++) {
        if (a[i] != b[i]) return false;
    }

    return true;
}

struct antennae
{
    antennae() { pipe(from_queen); pipe(to_queen); }
    int from_queen[2];
    int to_queen[2];
};

void ask_for_task(antennae *pipes, int num_bees, int worker)
{
    (void)num_bees;
    cout << "Worker bee " << worker << " asked queen for something to do\n";
    write(pipes[worker].to_queen[WRITE], AVAILABLE, strlen(AVAILABLE));
}

char *get_task(antennae *pipes, int num_bees, int worker)
{
    (void)num_bees;

    char *buf = new char[BUF];
    for (int i = 0; i < BUF; i++) buf[i] = 0;
    if (read(pipes[worker].from_queen[READ], buf, BUF) == -1) {
        perror("read()");
    }

    return buf;
}

void do_task(char *task)
{
    cout << "Buzz: " << task << endl;
    sleep(1);
}

void worker_bee_buzz(antennae *pipes, int num_bees, int worker)
{
    ask_for_task(pipes, num_bees, worker);

    while (true) {
        // buzzzzz
        char *task = get_task(pipes, num_bees, worker);
        cout << "Worker bee " << worker << " received from queen '" << task << "'\n";
        if (reallybadequal(task, (char *)COME_HOME)) {
            cout << "Worker bee " << worker << " coming home\n";
            return;
        } else if (reallybadequal(task, (char *)POISON)) {
            cout << "Worker bee " << worker << " killed by a bee eater.\n";
            raise(SIGSEGV);
        }

        do_task(task);
        delete[] task;
        ask_for_task(pipes, num_bees, worker);
    }
}

void refresh_workers(pid_t *pids, antennae *pipes, int num_bees);

int get_available_worker(pid_t *pids, antennae *pipes, int num_bees)
{
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;

    fd_set rfds;
    int num_avail = ERROR;
    
    do {
        if (pids != NULL) {
            refresh_workers(pids, pipes, num_bees);
        }
        FD_ZERO(&rfds);
        int max_fd = -1;
        for (int worker = 0; worker < num_bees; worker++) {
            int fd = pipes[worker].to_queen[READ];
            FD_SET(fd, &rfds);

            if (fd > max_fd) { max_fd = fd; }
        }

        num_avail = select(max_fd + 1, &rfds, NULL, NULL, &tv);
    } while (num_avail == 0);

    if (num_avail == ERROR) {
        perror("select()");
    }

    int available = ERROR;
    for (int worker = 0; worker < num_bees; worker++) {
        if (FD_ISSET(pipes[worker].to_queen[READ], &rfds)) {
            available = worker;
            break;
        }
    }

    char buf[BUF];
    if (read(pipes[available].to_queen[READ], buf, BUF) == ERROR) {
        perror("read()");
    }

    if (reallybadequal(buf, (char *)AVAILABLE)) {
        return available;
    }

    return ERROR;
}

void assign_task(antennae *pipes, int num_bees, int worker, string task)
{
    (void)num_bees;
    cout << "Queen bee told worker bee " << worker << " to '" << task << "'\n";
    write(pipes[worker].from_queen[WRITE], task.c_str(), task.length());
}

void retstart_worker(pid_t *pids, antennae *pipes, int num_bees, int worker)
{
    cout << "Queen has replaced worker " << worker << endl;
    pipes[worker] = antennae();
    pids[worker] = fork();

    if (pids[worker] < 0) {
        // error
        cerr << "Oh no, worker bee " << worker << " died.\n";
        return;
    } else if (pids[worker] == 0) {
        // child
        worker_bee_buzz(pipes, num_bees, worker);
        exit(0);
    } else {
        // parent
        return;
    }
}

void refresh_workers(pid_t *pids, antennae *pipes, int num_bees)
{
    for (int worker = 0; worker < num_bees; worker++) {
        int wstatus;
        if (waitpid(pids[worker], &wstatus, WNOHANG) == 0) {
            continue;
        }

        // process status
        if (WIFEXITED(wstatus)) {
            // worker exited normally
            int exit_status = WEXITSTATUS(wstatus);
            if (exit_status == 0) continue;
            cerr << "Worker " << worker << " exited normally with exit status: " << exit_status << endl;
        } else if (WIFSIGNALED(wstatus)) {
            // worker terminated by signal
            int terminate_signal = WTERMSIG(wstatus);
            cerr << "Worker " << worker << " terminated by signal: " << terminate_signal;
            if (WCOREDUMP(wstatus)) {
                // core dumped, probably want to get it
                cerr << " and had core dumped";
            }
            cerr << endl;
        } else if (WIFSTOPPED(wstatus)) {
            // worker stopped by signal
            int stop_signal = WSTOPSIG(wstatus);
            cerr << "Worker " << worker << " stopped by signal: " << stop_signal << endl;
        } else  if (WIFCONTINUED(wstatus)) {
            // worker continued by signal
            cerr << "Worker " << worker << " resumed by SIGCONT signal\n";
            continue;
        }

        retstart_worker(pids, pipes, num_bees, worker);
    }
}

int main()
{
    int num_bees = 0;
    cout << "Welcome to bee simulator! You are Queen Bee. How many bees do you want to send to work today?\n";
    cin >> num_bees;

    vector<string> tasks;
    string task;
    getline(cin, task); // cleanup
    cout << "What would you like the bees to do today?\n";
    srand(time(NULL));
    while (getline(cin, task)) {
        if ((rand() % 10)  > 4) {
            task = POISON;
        }
        tasks.push_back(task);
    }

    cout << "Sending bees out...\n";

    pid_t *pids = new pid_t[num_bees];
    antennae *pipes = new antennae[num_bees];

    for (int worker = 0; worker < num_bees; worker++) {
        pids[worker] = fork();

        if (pids[worker] < 0) {
            // error
            cerr << "Oh no, worker bee " << worker << " died.\n";
            continue;
        } else if (pids[worker] == 0) {
            // child
            worker_bee_buzz(pipes, num_bees, worker);
            exit(0);
        } else {
            // parent
            continue;
        }
    }

    sleep(1); // let worker bees fly off first
    cout << "Assigning tasks to bees...\n";

    for (unsigned int i = 0; i < tasks.size(); i++) {
        int worker = get_available_worker(pids, pipes, num_bees);
        assign_task(pipes, num_bees, worker, tasks[i]);
    }

    cout << "All tasks assigned. Telling bees to return home...\n";

    for (int i = num_bees; i > 0; i--) {
        int worker = get_available_worker(pids, pipes, num_bees);
        assign_task(pipes, num_bees, worker, COME_HOME);
    }

    for (int i = 0; i < num_bees; i++) {
        waitpid(pids[i], NULL, 0);
    }

    cout << "All bees safely home. Thank you for using bee simulator.\n";

    delete[] pids;
    delete[] pipes;
}

