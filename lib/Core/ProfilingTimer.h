#ifndef _PROFILINGTIMER_H
#define	_PROFILINGTIMER_H

#include "klee/Internal/System/Time.h"
#include <assert.h>


namespace klee {

    class ProfileTimer {
    public:

        ProfileTimer(bool _disable) : disable(_disable), started(false), startSeconds(0), total(0) {

        }

        ProfileTimer() : disable(true) {

        }

        void start() {
            if (disable) return;
            assert(!started);
            startSeconds = util::getWallTime();
            started = true;
        }

        double time() {
            if (disable) return 0;
            assert(!started);
            return total;
        }

        void stop() {
            if (disable) return;
            assert(started);
            
            total += (util::getWallTime() - startSeconds);
            started = false;
        }

        bool disable;
        bool started;
        double startSeconds;
        double total;
    };
    /*    class ProfileTimer {
        public:

            ProfileTimer(bool _disable) : disable(_disable), started(false), startMicroseconds(0), total(0) {

            }
            ProfileTimer() : disable(true) {

            }

            void start() {
                if (disable) return;
                assert(!started);
                sys::TimeValue now(0, 0), user(0, 0), sys(0, 0);
                sys::Process::GetTimeUsage(now, user, sys);
                startMicroseconds = now.usec();
                started = true;
            }

            uint64_t time() {
                if (disable) return 0;
                assert(!started);
                return total;
            }

            void stop() {
                if (disable) return;
                assert(started);

                sys::TimeValue now(0, 0), user(0, 0), sys(0, 0);
                sys::Process::GetTimeUsage(now, user, sys);
                total += (now.usec() - startMicroseconds);
                started = false;
            }

            bool disable;
            bool started;
            uint64_t startMicroseconds;
            uint64_t total;
        };
     * */
}


#endif

