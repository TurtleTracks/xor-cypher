#include <condition_variable>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdio.h>
#include <string>
#include <thread>
#include <queue> 
#include <chrono>
// limit threads to process at most 16kB data
const int kMaxBufferSz = 16384; 

char* key = nullptr;
int key_sz = 0;

std::mutex mtx;
std::condition_variable cv; 
int t_id = 0; 

void xor_stream(int thread_id, char* buffer, int buffer_sz, int rot) {
    char * encrypted_text = new char[buffer_sz];

    // xor buffer asynchronously
    for (int i = 0; i < buffer_sz; ++i) {
        char bite = 0;
        int idx = i * 8 + rot + i / key_sz;

        if ((idx % 8) == 0) {
            bite = key[(idx / 8) % key_sz]; // select byte without offset
        }  else {
            // bite overlaps two idxs,
            // use bit manipulation to capture appropriate bits
            int idx1 = (idx / 8) % key_sz;
            int idx2 = (idx / 8 + 1) % key_sz;
            int x = idx % 8; // shift amount
            bite |= key[idx1] << x | (((key[idx2] >> 1) & 0xef) >> (--x));
        }
        // encrypt byte, then update rotation index
        encrypted_text[i] = buffer[i] ^ bite;
    }
    
    // acquire lock for sequential writes to stdout
    std::unique_lock<std::mutex> lck(mtx);
    while (t_id != thread_id) {
        cv.wait(lck);
    }
    //std::cout.write(encrypted_text, buffer_sz);
    std::cout << std::endl << "Thread " << thread_id << " just completed." << std::endl;
    // increment t_id, then free lock and notify waiting threads
    t_id++;
    lck.unlock();
    cv.notify_all();

    // free resources
    delete[] buffer;
    delete[] encrypted_text;
}

void encrypt(int threads_wanted) {
    // determine number of threads to spawn based on user input and hardware
    // default to user input if hardware_concurrency() not supported
    unsigned int max_threads = std::thread::hardware_concurrency() > 0 ? 
        std::thread::hardware_concurrency() : threads_wanted; 
    unsigned int num_threads = threads_wanted > max_threads ? max_threads : threads_wanted;
    std::cout << "Thread limit is: " << num_threads << std::endl;
    
    // try to set buffer_sz large enough for one full rotation
    int buffer_sz = key_sz * key_sz * 8; 
    buffer_sz =  buffer_sz > kMaxBufferSz ? kMaxBufferSz : buffer_sz; 
    char* buffer = new char[buffer_sz];

    int rotations = 0;
    int read_sz = 0;
    int thread_count = 0;
    std::queue<std::thread> threads;

    // reopen stdin in binary mode
    freopen(nullptr, "rb", stdin);

    // read in bytes until there are none left
    while((read_sz = std::fread(buffer, sizeof(buffer[0]), buffer_sz, stdin)) > 0) {
        // wait for free threads
        if (threads.size() >= num_threads) {
            threads.front().join();
            threads.pop();
        }
        threads.push(std::thread(xor_stream, thread_count++, buffer, read_sz, rotations));

        // reset thread count if int_max reached
        if (thread_count == std::numeric_limits<int>::max()) {
            while(threads.size() > 0) {
                threads.front().join();
                threads.pop();
            }
            thread_count = 0;
            t_id = 0;
        }

        rotations = (rotations + (read_sz / key_sz)) % (key_sz * 8);
        buffer = new char[buffer_sz];
    }

    // join remaining threads
    while(threads.size() > 0) {
        threads.front().join();
        threads.pop();
    }
}

int main(int argc, char *argv[]) {
    int num_threads = 1;
    std::string key_path = "";
    bool bad_argument(false);

    for (int i = 0; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "-n") {
            try {
                num_threads = std::stoi(std::string(argv[i+1]));
                bad_argument = num_threads <= 0;
            } catch (const std::invalid_argument& ia) {
                std::cerr << "Invalid thread count: " << ia.what() << std::endl;
                bad_argument = true;
            } catch (const std::out_of_range& oor) {
                std::cerr << "Too many threads requested: " << oor.what() << std::endl;
                bad_argument = true;
            }
        } else if(std::string(argv[i]) == "-k") {
            key_path = std::string(argv[i+1]);
        }
    }

    if (bad_argument) {
        std::cerr << "Input thread count must be positive integral." << std::endl;
        return 0;
    }

    if (!key_path.empty()) {
        std::ifstream key_string;
        key_string.open(key_path, std::ios::binary | std::ios::ate);
        
        if (key_string.fail()) {
            std::cerr << "Keyfile could not be opened." << std::endl;
            return 0;
        }

        key_sz = key_string.tellg();
        key_string.seekg(0, std::ios::beg);
        key = new char[key_sz];
        key_string.read(key, key_sz);
        key_string.close();
    } 
    if (key_sz == 0) {
        key = new char[0]();
        key_sz = 0;
    }
    auto start = std::chrono::high_resolution_clock::now(); 
    try {
        encrypt(num_threads);
        delete[] key;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    auto microseconds = std::chrono::duration_cast<std::chrono::milliseconds>(finish-start);
    std::cout << std::endl << microseconds.count() << "ms\n";
    return 0;
}