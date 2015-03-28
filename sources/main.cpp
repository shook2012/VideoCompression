#include "headers/include_list.h"
#include "headers/defines.h"
#include "headers/commands.h"
#include "headers/networkhandler.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <exception>
#include <limits>
#include <string>
#include <mutex>
#include <queue>
#include <string.h>
#include <curses.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;
using namespace utilities;

void usage() {
}

bool argsContains(char **argv, const char *str) {
    while (*argv != nullptr) {
        if (!strcmp(*argv, str))
            return true;
        ++argv;
    }
    return false;
}

int32_t parseOptions(int32_t argc, char **argv) {
    int32_t opt;
    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
        case 'p':
            DATA->config.intValues.at("LISTENING_PORT") = atoi(optarg);
            break;
        case '?':
            usage();
            break;
        }
    }
    return (optind);
}

int32_t readConfiguration(const std::string &cf) {
    ifstream ifs(cf);
    std::string param_name, value, line;
    int32_t intvalue;
    while(ifs.good()) {
        getline(ifs, line);
        std::stringstream ss(line);
        ss >> param_name;
        ss >> value;
        try {
            intvalue = std::stoi(value);
            try {
                DATA->config.intValues.insert(
                            std::make_pair(param_name, intvalue));
            } catch (...) {
                reportError("Failed to read parameter " + param_name);
            }
        } catch (std::invalid_argument) {
            try {
                DATA->config.strValues.insert(
                            std::make_pair(param_name, value));
            } catch (...) {
                reportError("Failed to read parameter " + param_name);
            }
        }
    }
   return (0);
}

void superPeerRoutine(NetworkHandler &net_handler) {
    net_handler.start_listening(DATA->config.intValues.at("SUPERPEER_PORT"));
}

void initConfiguration() {
    std::shared_ptr<Data> data = DATA;
    DATA->state.can_accept = DATA->config.getIntValue("MAX_ACCEPTED_CHUNKS");
    DATA->config.working_dir = DATA->config.getStringValue("WD");
    if (data->config.working_dir == "") {
        char dirp[BUF_LENGTH];
        if (getcwd(dirp, BUF_LENGTH) == NULL) {
            data->config.working_dir = std::string(dirp);
        } else {
            data->config.working_dir = ".";
        }
    }
    data->config.IPv4_ONLY = false;
    int32_t x,y, y_space;
    getmaxyx(stdscr, y, x);
    y_space = y - 5;
    data->io_data.status_win = derwin(stdscr, y_space / 2 - 1, x, 3 + y_space / 2, 0);
    data->io_data.info_win = derwin(stdscr, y_space/ 2, 80, 3, 0);
    data->io_data.status_handler.changeWin(data->io_data.status_win);
    data->io_data.info_handler.changeWin(data->io_data.info_win);
    data->config.superpeer_addr = DATA->config.getStringValue("SUPERPEER_ADDR");
    data->io_data.changeLogLocation(DATA->config.working_dir + "/log_" +
                                    utilities::getTimestamp() + "_" +
                                    utilities::m_itoa(DATA->config.getIntValue("LISTENING_PORT")));
}

void initCommands(VideoState &state, NetworkHandler &net_handler) {
    // TODO: initialize more reasonably
    DATA->cmds.emplace(DEFCMD, new CmdDef);
    DATA->cmds.emplace(SHOW, new CmdShow(&state, &net_handler));
    DATA->cmds.emplace(START, new CmdStart(&state));
    DATA->cmds.emplace(LOAD, new CmdLoad(&state));
    DATA->cmds.emplace(SET, new CmdSet(&state));
    DATA->cmds.emplace(SET_CODEC, new CmdSetCodec(&state));
    DATA->cmds.emplace(SET_SIZE, new CmdSetChSize(&state));
    DATA->cmds.emplace(SET_FORMAT, new CmdSetFormat(&state));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(CONFIRM_HOST, new CmdConfirmHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(CONFIRM_PEER, new CmdConfirmPeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(ASK_PEER, new CmdAskPeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(ASK_HOST, new CmdAskHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(PING_PEER, new CmdPingPeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(PING_HOST, new CmdPingHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(DISTRIBUTE_PEER, new CmdDistributePeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(DISTRIBUTE_HOST, new CmdDistributeHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(GATHER_PEER, new CmdGatherNeighborsPeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(GATHER_HOST, new CmdGatherNeighborsHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(RETURN_PEER, new CmdReturnPeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(RETURN_HOST, new CmdReturnHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(GOODBYE_PEER, new CmdGoodbyePeer(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(GOODBYE_HOST, new CmdGoodbyeHost(&state, &net_handler)));
    DATA->net_cmds.insert(make_pair<CMDS, NetworkCommand *>(SAY_GOODBYE, new CmdSayGoodbye(&state, &net_handler)));
}

void cleanCommands(cmd_storage_t &cmds) {
    for (auto &c : cmds)
        delete c.second;
}

void periodicActions(NetworkHandler &net_handler) {
    if (DATA->neighbors.getNeighborCount() < DATA->config.getIntValue(
                "MAX_NEIGHBOR_COUNT")) {
        net_handler.obtainNeighbors();
    }
    net_handler.contactSuperPeer();
    DATA->periodic_listeners.applyTo(
                [&](Listener *l) { l->invoke(net_handler);  });
    DATA->neighbors.removeDirty();
}

int32_t main(int32_t argc, char **argv) {
	if (argc > 2) {
		usage();
    }

    initCurses();
    if (readConfiguration("CONF") == -1) {
        reportError("Error reading configuration!");
        return 1;
    }
    int32_t optidx = parseOptions(argc, argv);
    initConfiguration();
    DATA->config.working_dir += "/" +
            utilities::m_itoa(DATA->config.getIntValue("LISTENING_PORT"));
    if (OSHelper::prepareDir(DATA->config.working_dir, false) == -1) {
        reportError("Failed to prepare working directory.");
    }
    NetworkHandler net_handler;
    VideoState state(&net_handler);
    initCommands(state, net_handler);
    WINDOW *win = subwin(stdscr, 5, 80, 0, 0);
    wmove(win, 0,0);
    wprintw(win, "Distributed video compression tool.");
    wmove(win, 1, 0);
    wprintw(win, "Commands: %10s%10s%10s%10s", "F6 show", "F7 start", "F8 load", "F9 set", "F12 quit");
    curs_set(0);
    wrefresh(win);
    refresh();
    if (argsContains(argv + optidx, std::string("super").c_str())) {
        DATA->config.is_superpeer = true;
        std::thread thr ([&]() {
            superPeerRoutine(net_handler);
        });
        thr.detach();
    } else {
        std::thread thr ([&]() {
            net_handler.start_listening(DATA->config.intValues.at("LISTENING_PORT"));
        });
        thr.detach();
        std::thread thr2 ([&]() {
            while (1) {
                periodicActions(net_handler);
                sleep(1);
            }
        });
        thr2.detach();
        std::thread thr3(chunkProcessRoutine);
        thr3.detach();
        std::thread split_thr ([&]() {
            chunkSendRoutine(&net_handler);
        });
        split_thr.detach();
    }
    try {
        do{
        } while (!acceptCmd(DATA->cmds));
    } catch (exception e) {
       printw(e.what());
    }
    DATA->net_cmds.at(SAY_GOODBYE)->execute();
    cleanCommands(DATA->cmds);
    endwin();
	return (0);
}
