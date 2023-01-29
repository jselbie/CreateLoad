// CreateLoad.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace std;

struct ProgramState
{
    atomic_bool canRun;
    atomic_bool needToExit;

    mutex mutex;
    condition_variable cv;

    unsigned long long iterations;
};

struct Args
{
    bool useRealTime;
    bool useHardwareConcurrency;
    unsigned long numWorkerThreads;
    std::chrono::seconds runTimeSeconds;
};

extern void ProcessResult(double g, double h, double z);

void ElevateThreadToHighestPriority()
{
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
}

unsigned GetMaxThreads()
{
    return thread::hardware_concurrency() * 4;
}

bool ParseArguments(int argc, char** argv, Args& args)
{
    vector<string> argList;

    Args argsTemp = {};

    for (int i = 0; i < argc; i++)
    {
        string s = argv[i];
        transform(s.begin(), s.end(), s.begin(), tolower);
        argList.push_back(s);
    }
    argList.push_back("");

    for (int i = 1; i < argc;i++)
    {
        if (argList[i] == "--threads")
        {
            i++;
            unsigned long value = strtoul(argList[i].c_str(), nullptr, 10);
            if ((value == 0) || (value > GetMaxThreads()))
            {
                return false;
            }
            argsTemp.numWorkerThreads = value;
        }
        else if (argList[i] == "--rt")
        {
            argsTemp.useRealTime = true;
        }
        else if (argList[i] == "--hc")
        {
            argsTemp.useHardwareConcurrency = true;
            argsTemp.numWorkerThreads = thread::hardware_concurrency();
        }
        else if (argList[i] == "--time")
        {
            i++;
            long long value = strtoll(argList[i].c_str(), nullptr, 10);
            if ((value <= 0) || (value > 86400))
            {
                return false;
            }
            argsTemp.runTimeSeconds = std::chrono::seconds(value);
        }
    }

    if (argsTemp.numWorkerThreads == 0)
    {
        argsTemp.numWorkerThreads = 1;
    }
    if (argsTemp.runTimeSeconds == std::chrono::seconds(0))
    {
        argsTemp.runTimeSeconds = std::chrono::seconds(15);
    }

    args = argsTemp;
    return true;
}

// waits for the main thread to get into its main loop
void WaitForStartSignal(ProgramState& ps)
{
    unique_lock<mutex> lock(ps.mutex);
    while (ps.canRun == false)
    {
        ps.cv.wait(lock);
    }
}

void SignalStart(ProgramState& ps)
{
    unique_lock<mutex> lock(ps.mutex);
    ps.canRun = true;
    ps.cv.notify_all();
}

void DoLoad(const Args& args, ProgramState& ps)
{
    WaitForStartSignal(ps);
    unsigned long long iterations = 0;

    if (args.useRealTime)
    {
        ElevateThreadToHighestPriority();
    }
    while (ps.needToExit == false)
    {
        double other = 0;
        for (int i = 0; i < 1000000; i++)
        {
            // do a bunch of random math
            double g = rand() % 1000 + 1;
            double h = rand() % 1000 + 1;
            double z = g / h + g * h;
            if (z == 3.14)
            {
                ProcessResult(g,h,z);
            }

            int x = rand() % 1000 + 1;
            int y = rand() % 1000 + 2;
            int w = x / y + x * y;
            if (w == 999)
            {
                ProcessResult(x, y, w);
            }
        }
        iterations++;
    }

    {
        std::unique_lock<std::mutex> lck(ps.mutex);
        ps.iterations += iterations;
    }
}

void RunUntilTimeout(std::chrono::seconds duration, ProgramState& ps)
{
    ElevateThreadToHighestPriority();
    auto now = std::chrono::system_clock::now();
    auto stop = now + duration;
    bool hasStarted = false;
    while (now < stop)
    {
        if (hasStarted == false)
        {
            hasStarted = true;
            SignalStart(ps);
        }
        auto d = (stop - now)/2;
        this_thread::sleep_for(d);
        now = std::chrono::system_clock::now();
    }

    ps.needToExit = true;
}

void PrintUsage(const char* argv0)
{
    cout << "Usage: " << argv0 << " [-hc] [--threads] [--time] [--rt]" << "\n";
    cout << "\n";
    cout << "  --threads: The number of threads to use (default is 1)\n";
    cout << "       --hc: Use a number of threads equivalent to the number of logical CPUs on this system\n";
    cout << "       --rt: Run all threads at real time priority\n";
    cout << "     --time: Number of seconds to run for\n";
}

int main(int argc, char** argv)
{
    Args args = {};
    ProgramState ps = {};
    if (ParseArguments(argc, argv, args) == false)
    {
        PrintUsage(argv[0]);
        return 1;
    }

    cout << "Running with " << args.numWorkerThreads << " threads\n";
    cout << "Running with " << (args.useRealTime ? "Real Time" : "Normal") << " Priority\n";
    cout << "Running for  " << args.runTimeSeconds.count() << " seconds\n";
    this_thread::sleep_for(std::chrono::milliseconds(100));

    if (args.useRealTime)
    {
        SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
    }

    vector<thread> threadVec;
    for (unsigned long i = 0; i < args.numWorkerThreads; i++) {
        auto lambda = [&args, &ps]() {
            DoLoad(args, ps);
        };
        thread t = thread(lambda);
        threadVec.push_back(move(t));
    }

    RunUntilTimeout(args.runTimeSeconds, ps);

    for (unsigned long i = 0; i < args.numWorkerThreads; i++) {
        threadVec[i].join();
    }

    std::cout << "Executed: " << ps.iterations << " loops\n";

    return 0;
}

void ProcessResult(double g, double h, double z)
{
    if (g == h)
    {
        cout << "no-op\n";
    }
}