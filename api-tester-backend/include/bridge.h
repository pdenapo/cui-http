#ifndef BRIDGE_H
#define BRIDGE_H

typedef struct {
    int status_code;
    double latency_ms;
    char* body; // Allocated via malloc in D, freed via free_response() in C
} HttpResponse;

int init_db(const char* db_path);
HttpResponse execute_and_save(const char* method, const char* url, const char* body);
void free_response(HttpResponse* res);

void say_hello();
int square(int x);
bool initDRuntime();
bool terminateDRuntime();


#endif
