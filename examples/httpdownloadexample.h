#include "httplib.h" // single header HTTP library: https://github.com/yhirose/cpp-httplib, optional HTTPS support with OpenSSL, MbedTLS or wolfSSL
#include <thread>
#include <string>

class NativeDownloadExample {
public:
    yuby::AudioInMemoryHeader *header;
    
    NativeDownloadExample() {
        header = (yuby::AudioInMemoryHeader *)malloc(sizeof(yuby::AudioInMemoryHeader));
        if (!header) return; else header->totalSizeBytes = header->status = header->firstChunkAddress = 0;
        header->retainCount = 1;
        backgroundThread = std::thread(&NativeDownloadExample::backgroundThreadFunction, this);
    }
    
    ~NativeDownloadExample() {
        if (backgroundThread.joinable()) backgroundThread.join();
        if (!header) return;
        yuby::AudioInMemoryChunk *c = (yuby::AudioInMemoryChunk *)header->firstChunkAddress;
        while (c) {
            yuby::AudioInMemoryChunk *next = (yuby::AudioInMemoryChunk *)c->nextChunkAddress;
            free(c);
            c = next;
        }
        free(header);
    }
    
private:
    std::thread backgroundThread;
    
    void backgroundThreadFunction() {
        httplib::Client *client = new httplib::Client("http://localhost:3000");
        client->set_read_timeout(0, 100 * 1000); // 100 ms
        httplib::ClientImpl::StreamHandle stream = client->open_stream("GET", "/web/files/fashion-is-my-life.mp3");
        if (!stream.is_valid() || (stream.response->status != 200)) { header->status = 2; return; }
        
        int64_t filesizeBytes = (int64_t)stream.response->get_header_value_u64("Content-Length"), *nextAddress = &header->firstChunkAddress, downloadedBytes = 0;
        if (filesizeBytes > 0) header->totalSizeBytes = filesizeBytes;
            
        while (true) {
            yuby::AudioInMemoryChunk *c = (yuby::AudioInMemoryChunk *)malloc(sizeof(yuby::AudioInMemoryChunk) + 65536);
            if (!c) { header->status = 2; break; } else c->nextChunkAddress = 0;
            c->payloadSizeBytes = stream.read((char *)&c->payload, 65536);
            if (c->payloadSizeBytes <= 0) {
                if (stream.get_read_error() != httplib::Error::Success) header->status = 2; // eof or error
                break;
            }
            downloadedBytes += c->payloadSizeBytes;
            *nextAddress = (int64_t)c;
            nextAddress = &c->nextChunkAddress;
        }
        
        if (header->totalSizeBytes == 0) header->totalSizeBytes = downloadedBytes;
        if (header->status == 0) header->status = 1;
    }
};

static void runNativeDownloadExample(void *playerObj, unsigned int openFlags) {
    NativeDownloadExample *exampleDownloader = new NativeDownloadExample();
    std::string url = "audioinmemory://" + std::to_string(reinterpret_cast<std::uintptr_t>(exampleDownloader->header));
    yuby::Open(playerObj, url.c_str(), openFlags);
    // exampleDownloader should be valid while the player has this opened, delete exampleDownloader later
}
