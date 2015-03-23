#include "headers/defines.h"
#include "headers/include_list.h"
#include <sstream>

void TransferInfo::print() {
    reportStatus("Path: " + path);
    reportStatus("Chunk name: " + name);
    MyAddr srca(src_address), a(address);
    reportStatus("SRC ADDR: " + srca.get());
    reportStatus("ADDR: " + a.get());
}

void TransferInfo::invoke(NetworkHandler &handler) {
    if (--time_left <= 0) {
        if ((--tries_left > 0) && (handler.checkNeighbor(address) != -1)) {
            time_left = DATA->config.getValue(
                        "COMPUTATION_TIMEOUT");
            return;
        } else {}
        try {
            if (DATA->chunks_returned.contains(getHash())) {
                return;
            }
            reportError(name + ": Still in queue, resending.");
            pushChunkSend(this);
            time_left = DATA->config.getValue(
                        "COMPUTATION_TIMEOUT");
            tries_left = DATA->config.getValue(
                        "TRIES_BEFORE_RESEND");
        } catch (...) {}
    }
}

int32_t TransferInfo::send(int32_t fd) {
    if (sendSth(chunk_size, fd) == -1) {
        reportDebug("Failed to send size.", 1);
        return -1;
    }

    if (sendSth(sent_times, fd) == -1) {
        reportDebug("Failed to send times sent.", 1);
        return -1;
    }

    if (sendInt32(fd, receiving_time) == -1) {
        reportDebug("Failed to send encoding_time.", 1);
        return -1;
    }

    if (sendInt32(fd, sending_time) == -1) {
        reportDebug("Failed to send encoding_time.", 1);
        return -1;
    }

    if (sendInt32(fd, encoding_time) == -1) {
        reportDebug("Failed to send encoding_time.", 1);
        return -1;
    }

    if (sendSth(address, fd) == -1) {
        reportDebug("Failed to send source address.", 1);
        return -1;
    }

    if (sendSth(src_address, fd) == -1) {
        reportDebug("Failed to send source address.", 1);
        return -1;
    }

    if (sendString(fd, job_id) == -1) {
        reportDebug("Failed to send job id.", 1);
        return -1;
    }

    if (sendString(fd, name) == -1) {
        reportDebug("Failed to send name.", 1);
        return -1;
    }

    if (sendString(fd, original_extension) == -1) {
        reportDebug("Failed to send original extension.", 1);
        return -1;
    }

    if (sendString(fd, desired_extension) == -1) {
        reportDebug("Failed to send desired extension.", 1);
        return -1;
    }

    if (sendString(fd, output_codec) == -1) {
        reportDebug("Failed to send codec.", 1);
        return -1;
    }

    if (sendString(fd, timestamp) == -1) {
        reportDebug("Failed to send codec.", 1);
        return -1;
    }
    return 0;
}

int32_t TransferInfo::receive(int32_t fd) {
    if (recvSth(chunk_size, fd) == -1) {
        reportDebug("Failed to receive size.", 1);
        return -1;
    }

     if (recvSth(sent_times, fd) == -1) {
        reportDebug("Failed to receive times sent.", 1);
        return -1;
    }

     if (receiveInt32(fd, receiving_time) == -1){
         reportDebug("Failed to receive encoding time.", 1);
         return -1;
     }

     if (receiveInt32(fd, sending_time) == -1){
         reportDebug("Failed to receive encoding time.", 1);
         return -1;
     }

     if (receiveInt32(fd, encoding_time) == -1){
         reportDebug("Failed to receive encoding time.", 1);
         return -1;
     }

    struct sockaddr_storage srca;

    if (recvSth(srca, fd) == -1) {
        reportDebug("Failed to receive address.", 1);
        return -1;
    }
    address = srca;

    if (recvSth(srca, fd) == -1) {
        reportDebug("Failed to receive source address.", 1);
        return -1;
    }

    src_address = srca;

    if ((job_id = receiveString(fd)).empty()) {
        reportDebug("Failed to receive job id.", 1);
        return -1;
    }

    if ((name = receiveString(fd)).empty()) {
        reportDebug("Failed to receive name.", 1);
        return -1;
    }

    if ((original_extension = receiveString(fd)).empty()) {
        reportDebug("Failed to receive original extension.", 1);
        return -1;
    }

    if ((desired_extension = receiveString(fd)).empty()) {
        reportDebug("Failed to receive desired extension.", 1);
        return -1;
    }

    if ((output_codec = receiveString(fd)).empty()) {
        reportDebug("Failed to receive codec.", 1);
        return -1;
    }

    if ((timestamp = receiveString(fd)).empty()) {
        reportDebug("Failed to receive codec.", 1);
        return -1;
    }
    return 0;
}

std::string TransferInfo::toString() {
    std::stringstream ss;
    ss << "----------------------------------" << std::endl;
    ss << "Name: " << name << std::endl;
    ss << "Times sent: " << sent_times << std::endl;
    ss << "Processing time: " << processing_time << std::endl;
    ss << "Sending time: " << sending_time << std::endl;
    ss << "Receiving time: " << receiving_time << std::endl;
    ss << "Encoding time: " << encoding_time << std::endl;
    ss << "Encoded by: " << MyAddr(address).get() << std::endl;
    ss << "---------------------------------" << std::endl;
    return ss.str();
}

std::string TransferInfo::getHash() {
    return (name + job_id);
}

bool TransferInfo::equalsTo(Listener *that) {
    return (getHash() == that->getHash());
}
