#include <string>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <chrono>
#include <vector>
#include <deque>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <sys/socket.h>
#include <stdio.h>
#include <time.h>
#include <curses.h>

#ifndef DEFINES_H
#define DEFINES_H
#define DEBUG_LEVEL 2
#define STATUS_LENGTH 10
#define WD "/home/vojcek/WD"
#define INFO_LINES 15
#define SUPERPEER_ADDR "127.0.0.1"
#define LINE_LENGTH 80
#define BUF_LENGTH 256
#define DEFAULT 1
#define RED 2
#define GREEN 3
#define GREY 4
#define BLUE 5
#define YELLOWALL 6
#define GREYALL 7
#define CYANALL 8
#define INVERTED 9
#define COLOR_GREY 100
#define BG 20
#define BG_COL COLOR_BLACK
#define ST_Y 10
#define DATA Data::getInstance()
#define show(x, y) wprintw(DATA->io_data.info_win, "%15s%35s\n", x, y);

class NetworkHandler;
template <typename Measure_inT = std::chrono::milliseconds>
struct Measured {
    template <typename FuncT, typename ... args>
    static typename Measure_inT::rep exec_measure(FuncT func, args && ... arguments) {
        std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
        func(std::forward<args>(arguments) ...);
        Measure_inT duration = std::chrono::duration_cast<Measure_inT>
                                    (std::chrono::system_clock::now() - start);
        return duration.count();
    }
};

template <typename T>
void applyToVector(std::vector<T *> vec, void (*op)(T *)) {
    std::for_each(vec.begin(), vec.end(), op);
}

namespace utilities {
    std::string getTimestamp();
}

typedef struct FileInfo {
    std::string fpath, codec, extension, basename, format;
    double fsize, duration, bitrate;
} finfo_t;

enum MSG_T { ERROR, PLAIN, SUCCESS, DEBUG };
enum CONN_T { OUTGOING, INCOMING };
enum RESPONSE_T { ACK_FREE, ACK_BUSY, AWAITING, ABORT };
enum CMDS { TERM, DEFCMD, SHOW, START, LOAD,
            SET, SET_CODEC, SET_SIZE,
          ASK, RESPOND, REACT,
          ASK_PEER, ASK_HOST,
          CONFIRM_PEER, CONFIRM_HOST,
          PING_PEER, PING_HOST,
          TRANSFER_PEER, TRANSFER_HOST };
class Command;
class NetworkCommand;
typedef std::map<std::string, std::string> configuration_t;
typedef std::map<CMDS, Command *> cmd_storage_t;
typedef std::map<CMDS, NetworkCommand *> net_cmd_storage_t;

class Listener {
public:
    virtual void invoke(NetworkHandler &handler) = 0;
    virtual std::string getHash() = 0;
};

struct MyAddr {
    std::string addr;
    int port;
    void print();
    std::string get();
    MyAddr(struct sockaddr_storage &addr);
};

class HistoryStorage {
    std::vector<std::string> history;
    std::string filename;
    unsigned int c_index;
public:
    HistoryStorage(const std::string &fn);
    void prev();
    void next();
    void save(std::string line);
    void write();
    std::string &getCurrent();
};

typedef std::pair<std::string, MSG_T> printable_pair_T;

class WindowPrinter {
    WINDOW *win;
    std::deque<printable_pair_T> q;
    bool bolded;
public:
    enum DIRECTION {UP, DOWN} direction;
    enum START {TOP, BOTTOM} start;
    WindowPrinter(DIRECTION dir, bool b, START st): bolded(b), direction(dir), start(st){
        win = stdscr;
    }
    void changeWin(WINDOW *nwin);
    void add(std::string msg, MSG_T type);
    void print();
};

struct State {
    bool enough_neighbors = false;
    bool working = false;
    std::atomic_int can_accept;
};


struct IO_Data {
    IO_Data(): info_handler(WindowPrinter::DOWN, false, WindowPrinter::TOP),
        status_handler(WindowPrinter::UP, true, WindowPrinter::BOTTOM) {}
    WindowPrinter info_handler, status_handler;
    int status_y = 0, perc_y = 0, question_y = 0;
    WINDOW *status_win, *info_win;
};

struct Mutexes_Data {
    std::mutex I_mtx, O_mtx, report_mtx, chunk_mtx, send_mtx;
    std::condition_variable IO_cond, chunk_cond, send_cond;
    bool using_I = false, using_O = false, process_deq_used = false, send_deq_used = false;
};

struct Configuration {
    bool is_superpeer = false, IPv4_ONLY;
    std::map<std::string, int> intValues;
    std::string working_dir, superpeer_addr;
    int getValue(std::string key);
};

struct TransferInfo;
struct Data {
    static std::shared_ptr<Data> getInstance() {
        if(!inst) {
            inst = std::shared_ptr<Data>(new Data);
        }
        return inst;
    }

    cmd_storage_t cmds;
    net_cmd_storage_t net_cmds;
    IO_Data io_data;
    Mutexes_Data m_data;
    Configuration config;
    State state;
    std::map<std::string, TransferInfo *> waiting_chunks;
    std::deque<TransferInfo *> chunks_to_encode;
    std::deque<TransferInfo *> chunks_to_send;
    std::map<std::string, Listener *> periodic_listeners; //TODO: hashmap, so easy deletion?
    static std::vector<std::string> getKnownCodecs() {
        return {"libx264", "msmpeg"};
    }
    static std::map<wchar_t, CMDS> getCmdMapping() {
        return {
            { KEY_F(6), SHOW },
            { KEY_F(7), START },
            { KEY_F(8), LOAD },
            { KEY_F(10), ASK },
            { KEY_F(9), SET } };
    }

private:
    static std::shared_ptr<Data> inst;
};

struct VideoState {
    finfo_t finfo;
    size_t secs_per_chunk, c_chunks, chunk_size;
    std::string dir_location, job_id, o_format, o_codec;
    bool working, splitting_ongoing, split_deq_used;
    int to_send;
    std::mutex split_mtx;
    std::condition_variable split_cond;
    NetworkHandler *net_handler;
    VideoState(NetworkHandler *nh): secs_per_chunk(0), c_chunks(0), chunk_size(DATA->config.getValue("CHUNK_SIZE")),
    dir_location(DATA->config.working_dir), o_format("mkv"), o_codec("h264"),
      working(false), splitting_ongoing(false), split_deq_used(false) {
        net_handler = nh;
    }

    int split();
    int join();
    void printVideoState();
    void changeChunkSize(size_t nsize);
    void loadFileInfo(finfo_t &finfo);
    void resetFileInfo();
    void abort();
    void pushChunk(std::string path);
    void transferChunk();
};

void splitTransferRoutine(VideoState *st);
void chunkProcessRoutine();
void pushChunkProcess(TransferInfo *ti);
void pushChunkSend(TransferInfo *ti);

struct NeighborInfo : public Listener {
    struct sockaddr_storage address;
    int intervals;
    //quality
    bool confirmed;
    bool active;
    bool free;

    void printInfo();
    virtual void invoke(NetworkHandler &handler);
    virtual std::string getHash();
    virtual ~NeighborInfo() {}

    NeighborInfo(struct sockaddr_storage &addr): intervals(DATA->config.getValue("CHECK_INTERVALS")), free(true) {
        address = addr;
    }
};

struct TransferInfo : public Listener {
    bool addressed;
    int chunk_count, chunk_size, time_left;
    time_t sent_time;
    struct sockaddr_storage address;
    std::string job_id;
    std::string name; //including extension
    std::string output_format;

    virtual void invoke(NetworkHandler &handler);
    virtual std::string getHash();
    TransferInfo(struct sockaddr_storage addr, std::string ji, std::string of, std::string n):
        time_left(DATA->config.intValues.at("COMPUTATION_TIMEOUT")), job_id(ji),
        name(n), output_format(of) {
        address = addr;
        addressed = true;
    }

    TransferInfo(std::string ji, std::string of, std::string n):
        time_left(DATA->config.intValues.at("COMPUTATION_TIMEOUT")), job_id(ji),
        name(n), output_format(of) {
        addressed = false;
    }

    virtual ~TransferInfo() {}
};


#endif
