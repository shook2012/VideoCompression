#include "headers/networkhandler.h"
#include "headers/include_list.h"
#include "headers/commands.h"
#include "headers/handle_IO.h"
#include "headers/defines.h"

#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <thread>
#include <algorithm>
#include <sys/fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <err.h>
#include <string.h>

using namespace std;
using namespace utilities;

void NetworkHandler::spawnOutgoingConnection(struct sockaddr_storage addri,
                                    int32_t fdi, vector<CMDS> cmds, bool async, void *data) {
    std::thread handling_thread([=]() {
        CMDS action;
        int32_t i;
        int32_t fd = fdi;
        struct sockaddr_storage addr = addri;
        bool response;
        for (auto cmd : cmds) {
            if ((sendCmd(fd, cmd)) == -1) {
                reportDebug("Failed to send command.", 2);
                break;
            }

            if (recvSth(action, fd) == -1) {
                reportDebug("Failed to get response.", 2);
                break;
            }
            response = true;
            if (action == TERM){
                sendSth(response, fd);
                break;
            }
           try {
               if (DATA->net_cmds.find(action) == DATA->net_cmds.end()) {
                   response = false;
                   if (sendSth(response, fd) == -1) {
                   reportDebug("Error while processing cmd.", 1);
                   }
                   break;
               }
               NetworkCommand *command = DATA->net_cmds.at(action);
               if (command == nullptr) {
                   response = false;
                   if (sendSth(response, fd) == -1) {
                       reportDebug("Error while processing cmd.", 1);
                   }
                   throw new std::out_of_range("Unrecognized command.");
               }
               if (sendSth(response, fd) == -1) {
                   reportDebug("Error while processing cmd.", 1);
                   break;
               }
               if (!command->execute(fd, addr, data)) {
                   reportError(command->getName());
                   throw new std::runtime_error(
                                           "Command was not completed successfuly");
               }
            }catch (std::exception *e) {
               reportDebug("Error while communicating: " + std::string(e->what()), 1);
               break;
           }
        }
        close(fd);
    });
    if (async) {
        handling_thread.detach();
        return;
    }
    handling_thread.join();
}

void NetworkHandler::spawnIncomingConnection(struct sockaddr_storage addri,
                                    int32_t fdi, bool async) {
    std::thread handling_thread([=]() {
        CMDS action;
        ssize_t r;
        int32_t fd = fdi;
        struct sockaddr_storage addr = addri;
        bool response;
        while ((r = read(fd, &action, sizeof (action))) == sizeof(action)) {
        response = true;
       try {
           if (DATA->net_cmds.find(action) == DATA->net_cmds.end()) {
               response = false;
               if (sendSth(response, fd) == -1) {
                   reportDebug("Error while processing cmd.", 1);
               }
               break;
           }
           NetworkCommand *cmd = DATA->net_cmds.at(action);
           reportDebug(cmd->getName(), 3);
           if (cmd == nullptr) {
               response = false;
               if (sendSth(response, fd) == -1) {
               reportDebug("Error while processing cmd.", 1);
               }
               throw exception();
           }
           if (sendSth(response, fd) == -1) {
               reportDebug("Error while processing cmd.", 1);
               break;
           }
           if (!cmd->execute(fd, addr, nullptr)) {
               reportDebug("Failed to process: " + cmd->getName(), 3);
               throw exception();
           }
        }catch (exception e) {
           reportDebug("Error while communicating: Unrecognized command."
                       + std::string(e.what()), 1);
           break;
           }
        }
        close(fd);
    });
    if (async) {
        handling_thread.detach();
        return;
    }
    handling_thread.join();
}

int32_t NetworkHandler::start_listening(int32_t port) {
    int32_t sock, accepted, ip6_only = 0, reuse = 1;
    struct sockaddr_in6 in6;
    socklen_t in6_size = sizeof (in6), psize = sizeof (struct sockaddr_storage);
    struct sockaddr_storage peer_addr;
    bzero(&in6, sizeof (in6));
    bzero(&peer_addr, in6_size);
    bzero(&in6.sin6_addr.s6_addr, 16);
    in6.sin6_family = AF_INET6;
    in6.sin6_port = htons(port);
    in6.sin6_addr.s6_addr[0] = 0;

    if ((sock = socket(AF_INET6, SOCK_STREAM, 6)) == -1) {
        reportDebug("Failed to create listening socket." + std::string(strerror(errno)), 1);
        return (-1);
    }

    if (setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY,
                    &ip6_only, sizeof(ip6_only)) == -1) {
        reportDebug("Failed to set option to listening socket." + std::string(strerror(errno)), 1);
        return (-1);
    }

    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
            &reuse, sizeof (reuse)) == -1) {
        reportDebug("Failed to set option to listening socket." + std::string(strerror(errno)), 1);
        return (-1);
   }
    struct linger so_linger;
    so_linger.l_onoff = 1;
    so_linger.l_linger = 10;
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER,
            &so_linger, sizeof (so_linger)) == -1) {
        reportDebug("Failed to set option to listening socket." + std::string(strerror(errno)), 1);
        return (-1);
   }

    if (bind(sock, (struct sockaddr *) &in6, in6_size) == -1) {
        reportDebug("Failed to bind the listening socket." + std::string(strerror(errno)), 1);
        return (-1);
    }

    if (listen(sock, SOMAXCONN) == -1) {
        reportDebug("Failed to start listen on the socket." + std::string(strerror(errno)), 1);
        return (-1);
    }
    for (;;) {
        if ((accepted = accept(sock, (struct sockaddr *) &peer_addr, &psize)) == -1) {
            reportDebug("Failed to accept connection." + std::string(strerror(errno)), 1);
            continue;
        }
        spawnIncomingConnection(peer_addr, accepted, true);
    }
}

void NetworkHandler::contactSuperPeer() {
    struct sockaddr_storage addr;
    if (DATA->config.IPv4_ONLY)
        addr = networkHelper::addr2storage(DATA->config.superpeer_addr.c_str(), DATA->config.intValues.at("SUPERPEER_PORT"), AF_INET);
    else
        addr = networkHelper::addr2storage(DATA->config.superpeer_addr.c_str(), DATA->config.intValues.at("SUPERPEER_PORT"), AF_INET6);
    int32_t sock =  checkNeighbor(addr);
    if (sock == -1) return;
    spawnOutgoingConnection(addr, sock, { PING_PEER }, false, nullptr);
}

int32_t NetworkHandler::checkNeighbor(struct sockaddr_storage addr) {
    int32_t sock;
    CmdAskPeer cmd(nullptr, nullptr);
    if ((sock = cmd.connectPeer(&addr)) == -1) {
        reportDebug("Failed to check neighbor."  + MyAddr(addr).get(), 3);
        //DATA->neighbors.removeNeighbor(addr);
        return -1;
    }
    DATA->neighbors.setInterval(addr,
                                DATA->config.getValue("CHECK_INTERVALS"));
    return sock;
}

int32_t NetworkHandler::getPotentialNeighborsCount() {
    potential_mtx.lock();
    int32_t count = potential_neighbors.size();
    potential_mtx.unlock();
    return count;
}

void NetworkHandler::gatherNeighbors(int32_t TTL,
        const struct sockaddr_storage &requester_addr,
        const struct sockaddr_storage &ngh_addr) {
    int32_t sock;
    if ((sock = checkNeighbor(ngh_addr)) == -1) {
        return;
    }
    MyAddr *requester_maddr = new MyAddr(requester_addr);
    requester_maddr->TTL = TTL;
    spawnOutgoingConnection(ngh_addr, sock, { GATHER_PEER }, true,
                (void *) requester_maddr);
}

void NetworkHandler::confirmNeighbor(struct sockaddr_storage addr) {
    int32_t sock =  checkNeighbor(addr);
    if (sock == -1) {
        return;
    }
    spawnOutgoingConnection(addr, sock, { CONFIRM_PEER }, false, nullptr);
}

void NetworkHandler::addNewNeighbor(bool potential, struct sockaddr_storage &addr) {
    if (potential) {
        potential_mtx.lock();
        potential_neighbors.push_back(addr);
        potential_mtx.unlock();
    } else {
        DATA->neighbors.addNewNeighbor(addr);
    }
}

void NetworkHandler::obtainNeighbors() {
    // if nobody to ask to, try to earn some more addresses
    if (!getPotentialNeighborsCount()) {
        reportDebug("Need to collect more potential neighbors", 4);
        collectNeighbors();
    }
    struct sockaddr_storage addr;
        if (!getPotentialNeighborsCount()) {
            return;
        }
        potential_mtx.lock();
            addr = potential_neighbors.back();
            potential_neighbors.pop_back();
        potential_mtx.unlock();
        confirmNeighbor(addr);
}

/* tries to collect more potential neighbors

*/
void NetworkHandler::collectNeighbors() {
    //TODO: handle potential neighbors
    struct sockaddr_storage address;
    if (DATA->neighbors.getNeighborCount()) {
        for (auto &addr : DATA->neighbors.getNeighborAdresses(
                DATA->neighbors.getNeighborCount())) {
            MyAddr mad(addr);
            reportDebug("Trying neighbor. " + mad.get(), 4);
            askForAddresses(addr);
            if (getPotentialNeighborsCount())
                break;
        }
    }
    if ((!getPotentialNeighborsCount()) &&
            (!DATA->config.is_superpeer)) {
        reportDebug("Trying superpeer.", 4);
        if (DATA->config.IPv4_ONLY)
            address = networkHelper::addr2storage(DATA->config.superpeer_addr.c_str(), DATA->config.intValues.at("SUPERPEER_PORT"), AF_INET);
        else
            address = networkHelper::addr2storage(DATA->config.superpeer_addr.c_str(), DATA->config.intValues.at("SUPERPEER_PORT"), AF_INET6);
        askForAddresses(address);
    }
}

void NetworkHandler::askForAddresses(struct sockaddr_storage &addr) {
    socklen_t sock;
    CmdAskPeer cmd(nullptr, nullptr);
    if ((sock = cmd.connectPeer(&addr)) == -1) {
        reportDebug("Failed to establish connection.", 1);
        return;
    }
    spawnOutgoingConnection(addr, sock, { PING_PEER, ASK_PEER }, false, nullptr);
}