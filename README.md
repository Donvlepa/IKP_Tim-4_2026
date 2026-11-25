# IKP – Project 4

## Load Balancing: Priorities and Worker Failure

A university project developed for the Industrial Communication Protocols (IKP) course at the Faculty of Technical Sciences, academic year 2026/2027.

## Project Overview

The goal of this project is to develop a distributed system for scheduling and processing requests from an electrical power network.

The system consists of three types of processes: User (UR), Load Balancer (LB), and Worker (WR). Communication between processes is established through TCP sockets using WinSock2.

## Technologies

- C++
- Visual Studio
- WinSock2 (TCP/IP)
- Windows threads
- Critical sections, semaphores, and events
- Custom data structures without STL containers
- Git and GitHub

## Project Structure

| Component | Description |
|---|---|
| Common | Shared library containing data structures, models, communication protocol, and socket utilities. |
| LoadBalancer | Central server responsible for receiving, scheduling, and monitoring requests. |
| User | Client application responsible for sending requests and receiving responses. |
| Worker | Application responsible for processing assigned requests and sending heartbeat messages. |
| Tests | Automated tests for custom data structures and system functionality. |

## Planned Features

- TCP communication through port 5059.
- Three request priority levels: High, Medium, and Low.
- Priority-based scheduling with a 5:1 fairness policy.
- Custom linked queues and hash maps.
- Binary communication protocol.
- Concurrent request processing.
- Heartbeat messages sent every second.
- Worker failure detection after three seconds without a heartbeat.
- Automatic reassignment of unfinished requests.
- Rejection of outdated processing results.
- Prevention of duplicate responses.
- Thread synchronization and resource management.
- Graceful shutdown.

## Testing

The project will include:

- Unit tests for all custom data structures.
- Communication protocol tests.
- Priority scheduling tests.
- Integration tests.
- Worker failure and recovery tests.
- Stress testing with approximately 10,000 requests.
- CPU, memory, and network usage monitoring.
- Thread count and context switch monitoring.
- Memory and handle leak detection.

## Development Requirements

- Windows operating system.
- Visual Studio with Desktop Development with C++ workload.
- Windows SDK.
- Git.

## Project Status

**In Development**

The project is being implemented incrementally according to the approved system design. Each component will be individually implemented, tested, and committed to the Git repository.