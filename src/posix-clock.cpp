#include <node.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>

using namespace v8;

// Modified version of NODE_DEFINE_CONSTANT
#define ODC_DEFINE_CONSTANT(target, name, value)                              \
  do {                                                                        \
    v8::Isolate* isolate = target->GetIsolate();                              \
    v8::Local<v8::String> constant_name =                                     \
        v8::String::NewFromUtf8(isolate, name);                               \
    v8::Local<v8::Number> constant_value =                                    \
        v8::Number::New(isolate, static_cast<double>(value));                 \
    v8::PropertyAttribute constant_attributes =                               \
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete);    \
    (target)->ForceSet(constant_name, constant_value, constant_attributes);   \
  }                                                                           \
  while (0)

#define AVZ_FILL_TIMESPEC(target, sec, nsec) \
		(target)->Set(String::NewFromUtf8(isolate, "sec"), Number::New(isolate, sec)); \
		(target)->Set(String::NewFromUtf8(isolate, "nsec"), \
			Integer::NewFromUnsigned(isolate, static_cast<uint32_t>(nsec)));

#define AVZ_VALIDATE_ARG_CLOCKID(arg) \
		if(!(arg)->IsInt32()) { \
			isolate->ThrowException(Exception::Error( \
				String::NewFromUtf8(isolate, "Specified clockId is not supported on this system"))); \
			return; \
		}

void ClockGetTime(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = args.GetIsolate();

	if(args.Length() != 1) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong number of arguments")));
		return;
	}

	AVZ_VALIDATE_ARG_CLOCKID(args[0]);

	clockid_t clockId = args[0]->Int32Value();
	struct timespec ts;

	if(clock_gettime(clockId, &ts) != 0) {
		if(errno == EINVAL) {
			isolate->ThrowException(Exception::Error(String::NewFromUtf8(
				isolate, "Specified clockId is not supported on this system"
			)));
		} else {
			isolate->ThrowException(Exception::Error(
				String::Concat(String::NewFromUtf8(isolate, strerror(errno)), args[0]->ToString())
			));
		}

		return;
	}

	Local<Object> obj = Object::New(isolate);

	AVZ_FILL_TIMESPEC(obj, ts.tv_sec, ts.tv_nsec);

	args.GetReturnValue().Set(obj);
}

void ClockGetRes(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = args.GetIsolate();

	if(args.Length() != 1) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong number of arguments")));
		return;
	}

	AVZ_VALIDATE_ARG_CLOCKID(args[0]);

	clockid_t clockId = args[0]->Int32Value();
	struct timespec ts;

	if(clock_getres(clockId, &ts) != 0) {
		if(errno == EINVAL) {
			isolate->ThrowException(Exception::Error(String::NewFromUtf8(
				isolate, "Specified clockId is not supported on this system"
			)));
		} else {
			isolate->ThrowException(Exception::Error(
				String::Concat(String::NewFromUtf8(isolate, strerror(errno)), args[0]->ToString())
			));
		}

		return;
	}

	Local<Object> obj = Object::New(isolate);

	AVZ_FILL_TIMESPEC(obj, ts.tv_sec, ts.tv_nsec);

	args.GetReturnValue().Set(obj);
}

void ClockNanosleep(const FunctionCallbackInfo<Value>& args) {
	Isolate* isolate = args.GetIsolate();

	if(args.Length() != 3) {
		isolate->ThrowException(Exception::TypeError(
			String::NewFromUtf8(isolate, "Wrong number of arguments")));
		return;
	}

	AVZ_VALIDATE_ARG_CLOCKID(args[0]);

	clockid_t clockId = args[0]->Int32Value();
	int flags = args[1]->Int32Value();

	if(!args[2]->IsObject()) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Sleep time must be an object, e.g. {sec: 1212, nsec: 4344}"
		)));

		return;
	}

	struct timespec sleepTimeTs;
	struct timespec remainingTimeTs;

	Local<Object> objSleep = args[2]->ToObject();
	Local<Value> secValue = objSleep->Get(String::NewFromUtf8(isolate, "sec"));
	Local<Value> nsecValue = objSleep->Get(String::NewFromUtf8(isolate, "nsec"));

	if(!secValue->IsUndefined() && !secValue->IsUint32()) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Option `sec` must be unsigned integer"
		)));

		return;
	}

	if(!nsecValue->IsUndefined() && !nsecValue->IsUint32()) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Option `nsec` must be unsigned integer"
		)));

		return;
	}

	sleepTimeTs.tv_sec = (time_t)secValue->Uint32Value();
	sleepTimeTs.tv_nsec = (long)nsecValue->Uint32Value();

	if(sleepTimeTs.tv_nsec < 0 || sleepTimeTs.tv_nsec >= 1e9) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Option `nsec` must be in [0; 999999999]"
		)));

		return;
	}

#ifdef __linux__
	int err = clock_nanosleep(clockId, flags, &sleepTimeTs, &remainingTimeTs);

	if(err != 0) {
		if(err == EINVAL) {
			isolate->ThrowException(Exception::Error(String::NewFromUtf8(
				isolate, "Specified clockId is not supported on this system or invalid argument"
			)));
		} else if(err == EINTR) {
			/* stopped by signal - need to return remaining time */
			struct timespec *res;

			if(flags & TIMER_ABSTIME)
				res = &sleepTimeTs;
			else
				res = &remainingTimeTs;

			Local<Object> obj = Object::New(isolate);
			AVZ_FILL_TIMESPEC(obj, res->tv_sec, res->tv_nsec);
			return;
		} else {
			isolate->ThrowException(Exception::Error(
				String::Concat(String::Concat(
					String::NewFromUtf8(isolate, strerror(err)),
					String::NewFromUtf8(isolate, ": ")),
					args[0]->ToString()
				)
			));
		}
	}
#else
	if(clockId != CLOCK_REALTIME) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Only nanosleep(REALTIME) clock is supported by your OS"
		)));

		return;
	}

	if(flags & TIMER_ABSTIME) {
		isolate->ThrowException(Exception::Error(String::NewFromUtf8(
			isolate, "Flag nanosleep(TIMER_ABSTIME) is not supported by your OS"
		)));

		return;
	}

	int err = nanosleep(&sleepTimeTs, &remainingTimeTs);

	if(err == -1) {
		if(errno == EINTR) {
			Local<Object> obj = Object::New();
			AVZ_FILL_TIMESPEC(obj, remainingTimeTs.tv_sec, remainingTimeTs.tv_nsec);
			return scope.Close(obj);
		} else {
			isolate->ThrowException(Exception::Error(
				String::Concat(String::Concat(
					String::NewFromUtf8(isolate, strerror(err)),
					String::NewFromUtf8(isolate, ": ")),
					args[0]->ToString()
				)
			));

			return;
		}
	}
#endif

	return;
}

extern "C"
void init(Local<Object> exports, Local<Object> module) {
	NODE_SET_METHOD(module, "gettime", ClockGetTime);
	NODE_SET_METHOD(module, "getres", ClockGetRes);
	NODE_SET_METHOD(module, "nanosleep", ClockNanosleep);

#ifdef TIMER_ABSTIME
	ODC_DEFINE_CONSTANT(exports, "TIMER_ABSTIME", TIMER_ABSTIME); // for nanosleep
#endif

	ODC_DEFINE_CONSTANT(exports, "REALTIME", CLOCK_REALTIME);
	ODC_DEFINE_CONSTANT(exports, "MONOTONIC", CLOCK_MONOTONIC);

	/* Linux-specific constants */
#ifdef CLOCK_REALTIME_COARSE
	ODC_DEFINE_CONSTANT(exports, "REALTIME_COARSE", CLOCK_REALTIME_COARSE);
#endif

#ifdef CLOCK_MONOTONIC_COARSE
	ODC_DEFINE_CONSTANT(exports, "MONOTONIC_COARSE", CLOCK_MONOTONIC_COARSE);
#endif

#ifdef CLOCK_MONOTONIC_RAW
	ODC_DEFINE_CONSTANT(exports, "MONOTONIC_RAW", CLOCK_MONOTONIC_RAW);
#endif

#ifdef CLOCK_BOOTTIME
	ODC_DEFINE_CONSTANT(exports, "BOOTTIME", CLOCK_BOOTTIME);
#endif

#ifdef CLOCK_PROCESS_CPUTIME_ID
	ODC_DEFINE_CONSTANT(exports, "PROCESS_CPUTIME_ID", CLOCK_PROCESS_CPUTIME_ID);
#endif

#ifdef CLOCK_THREAD_CPUTIME_ID
	ODC_DEFINE_CONSTANT(exports, "THREAD_CPUTIME_ID", CLOCK_THREAD_CPUTIME_ID);
#endif

	/* FreeBSD-specific constants */
#ifdef CLOCK_REALTIME_FAST
	ODC_DEFINE_CONSTANT(exports, "REALTIME_FAST", CLOCK_REALTIME_FAST);
#endif

#ifdef CLOCK_REALTIME_PRECISE
	ODC_DEFINE_CONSTANT(exports, "REALTIME_PRECISE", CLOCK_REALTIME_PRECISE);
#endif

#ifdef CLOCK_MONOTONIC_FAST
	ODC_DEFINE_CONSTANT(exports, "MONOTONIC_FAST", CLOCK_MONOTONIC_FAST);
#endif

#ifdef CLOCK_MONOTONIC_PRECISE
	ODC_DEFINE_CONSTANT(exports, "MONOTONIC_PRECISE", CLOCK_MONOTONIC_PRECISE);
#endif

#ifdef CLOCK_UPTIME
	ODC_DEFINE_CONSTANT(exports, "UPTIME", CLOCK_UPTIME);
#endif

#ifdef CLOCK_UPTIME_FAST
	ODC_DEFINE_CONSTANT(exports, "UPTIME_FAST", CLOCK_UPTIME_FAST);
#endif

#ifdef CLOCK_UPTIME_PRECISE
	ODC_DEFINE_CONSTANT(exports, "THREAD_UPTIME_PRECISE", CLOCK_UPTIME_PRECISE);
#endif

#ifdef CLOCK_SECOND
	ODC_DEFINE_CONSTANT(exports, "THREAD_SECOND", CLOCK_SECOND);
#endif

#ifdef CLOCK_PROF
	ODC_DEFINE_CONSTANT(exports, "PROF", CLOCK_PROF);
#endif
}

//--

NODE_MODULE(posix_clock, init)
