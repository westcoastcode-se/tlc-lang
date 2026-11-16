#include <pthread.h>

typedef void *(*callback)(void *);

struct job {
    callback cb;
    void *arg;
};

static void *start(void *raw) {
    struct job *job = raw;
    return job->cb(job->arg);
}

// The thread is C's own. tlc does not see this pthread_create.
int run_in_thread(callback cb, void *arg) {
    pthread_t thread;
    struct job job = {cb, arg};
    if (pthread_create(&thread, 0, start, &job) != 0) {
        return 1;
    }
    pthread_join(thread, 0);
    return 0;
}
