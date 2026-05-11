# Online Judge System - Deployment Guide

Production deployment of the KVM-based online judge system for evaluating code submissions.

## Architecture Overview

```
Frontend (User Submissions)
         ↓
Contest Database (PostgreSQL/MySQL)
         ↓
Judge Queue Service (Redis/RabbitMQ)
         ↓
KVM Judge Hosts (1+ instances)
    ├─ Host: kvm-host process
    │   ├─ IPC: Shared memory (request/result)
    │   └─ Cgroup: Memory/CPU safety net
    └─ Guest: Executor daemon
         ├─ Compilation: gcc/g++
         ├─ Execution: process isolation
         ├─ Metrics: cgroup v2 monitoring
         └─ Verdict: 6-type classification
         ↓
Results Database (Verdict + Metrics)
         ↓
Frontend (Display Results)
```

## System Requirements

### Hardware
- **CPU**: 4+ cores recommended (2 cores minimum)
- **RAM**: 8GB+ per KVM host (supports 1-2 concurrent VMs)
- **Storage**: 100GB+ SSD for VM images and submission cache
- **Network**: 1Gbps link (for distributed judge farms)

### Software
- Linux kernel 6.0+ (cgroup v2, KVM)
- QEMU/KVM hypervisor
- GCC/G++ 10+ (in guest initrd)
- PostgreSQL 12+ (results storage)

### Kernel Configuration
```bash
# Enable KVM and cgroup v2
grep -E 'KVM|CGROUP' /boot/config-$(uname -r)

# Verify cgroup v2
mount | grep cgroup2

# Check cgroup features
ls /sys/fs/cgroup/
  - memory.max ✓
  - memory.peak ✓
  - cpu.max ✓
```

## Deployment Steps

### 1. Build Host System

```bash
# Clone and build kvm-host
git clone <repo>
cd kvm-host-repo
make -j$(nproc)

# Verify binary
ls -lh build/kvm-host
  # Should be ~2-5MB executable
```

### 2. Prepare Guest Image

```bash
# Build initrd with executor
cd initrd_builder
./build_initrd.sh manual

# Verify image
ls -lh build/initrd.img
  # Should be ~70-80MB

# Test: Extract and inspect
mkdir -p /tmp/initrd-check
cd /tmp/initrd-check
zcat ~/path/to/initrd.img | cpio -idmv
ls -la
  # Should contain: bin/, sbin/, lib/, usr/, dev/, proc/, sys/
ls -la usr/local/bin/executor
  # Should be ~1.1MB static binary
```

### 3. Configure Kernel Parameters

```bash
# Permanent configuration (/etc/sysctl.d/judge.conf)
vm.overcommit_memory = 1           # Allow cgroup limits
kernel.sched_migration_cost = 5000 # VM scheduling
net.core.somaxconn = 4096          # Queue depth
```

### 4. Set Up Cgroup Management

```bash
# Initialize cgroup v2 subsystems
echo "+memory +cpu" > /sys/fs/cgroup/cgroup.subtree_control
echo "+memory +cpu" > /sys/fs/cgroup/user.slice/cgroup.subtree_control

# Create judge cgroup
mkdir -p /sys/fs/cgroup/judge
echo "max" > /sys/fs/cgroup/judge/memory.max
echo "1000000:100000" > /sys/fs/cgroup/judge/cpu.max
```

### 5. Database Setup

```bash
# PostgreSQL schema for judge results
CREATE TABLE submissions (
    id SERIAL PRIMARY KEY,
    contest_id INT NOT NULL,
    user_id INT NOT NULL,
    problem_id INT NOT NULL,
    language VARCHAR(10) NOT NULL,
    source_code TEXT NOT NULL,
    submitted_at TIMESTAMP DEFAULT NOW()
);

CREATE TABLE verdicts (
    id SERIAL PRIMARY KEY,
    submission_id INT NOT NULL REFERENCES submissions(id),
    verdict VARCHAR(10) NOT NULL,  -- AC, WA, RE, TLE, MLE, CE
    cpu_time_seconds FLOAT,
    peak_memory_bytes BIGINT,
    compile_exit_code INT,
    program_exit_code INT,
    compile_stderr TEXT,
    program_stdout TEXT,
    program_stderr TEXT,
    verdict_time TIMESTAMP DEFAULT NOW()
);

CREATE INDEX idx_submission_verdict ON verdicts(submission_id);
CREATE INDEX idx_verdict_time ON verdicts(verdict_time);
```

### 6. Queue Service Setup

```bash
# Example: Redis-based queue
REDIS_HOST=localhost
REDIS_PORT=6379

# Redis schema
# Queue: judge:pending          → Pending submissions
# Queue: judge:processing       → Currently processing
# Hash:  judge:submission:{id}  → Submission metadata
```

### 7. Start Judge Hosts

```bash
#!/bin/bash
# start_judge_host.sh

KERNEL="/boot/vmlinuz-linux"
INITRD="./initrd_builder/build/initrd.img"
HOST_ID=${1:-1}
LISTEN_PORT=$((5000 + HOST_ID))

# Create host-specific cgroup
CGROUP="/sys/fs/cgroup/judge/host_$HOST_ID"
mkdir -p "$CGROUP"
echo "500M" > "$CGROUP/memory.max"
echo "2000000:1000000" > "$CGROUP/cpu.max"

# Start KVM with judge enabled
sudo cgexec -g memory,cpu:judge/host_$HOST_ID \
    ./build/kvm-host \
        -k "$KERNEL" \
        -i "$INITRD" \
        --judge-enable &

echo "Judge host $HOST_ID started (PID: $!)"
echo "Listen on port: $LISTEN_PORT"
```

## Load Balancing & Scaling

### Single Judge Host (Development)
```bash
# Start 1 judge host
bash start_judge_host.sh 1

# Submit directly via stdio/API
./build/test_kvm_integration  # Run tests in another terminal
```

### Multiple Judge Hosts (Production)

```bash
# Start 4 judge hosts
for i in 1 2 3 4; do
    bash start_judge_host.sh $i &
done

# Load balancer configuration (HAProxy/Nginx)
listen judge_hosts
    bind 0.0.0.0:5000
    mode tcp
    server judge1 localhost:5001
    server judge2 localhost:5002
    server judge3 localhost:5003
    server judge4 localhost:5004
    balance roundrobin
```

## Monitoring & Observability

### Metrics to Track

```
judge_submissions_total         # Total submissions processed
judge_submissions_seconds       # Submission latency (histogram)
judge_verdict_ac_total          # AC verdicts
judge_verdict_wa_total          # WA verdicts
judge_verdict_ce_total          # CE verdicts
judge_verdict_re_total          # RE verdicts
judge_verdict_tle_total         # TLE verdicts
judge_verdict_mle_total         # MLE verdicts
judge_cpu_seconds_bucket        # CPU time histogram
judge_memory_bytes_bucket       # Memory usage histogram
```

### Health Checks

```bash
#!/bin/bash
# health_check.sh

JUDGE_HOST="localhost:5000"

# Check KVM is running
timeout 30 ./build/test_ipc_local
if [ $? -ne 0 ]; then
    echo "KVM health check failed"
    exit 1
fi

# Check executor responding
timeout 30 ./build/test_judge_api
if [ $? -ne 0 ]; then
    echo "Executor health check failed"
    exit 1
fi

echo "Judge host healthy"
exit 0
```

### Prometheus Metrics Exporter

```c
// Example: Expose metrics on :9090
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void export_metrics(FILE *out) {
    fprintf(out, "# HELP judge_submissions_total Total submissions\n");
    fprintf(out, "# TYPE judge_submissions_total counter\n");
    fprintf(out, "judge_submissions_total 1234\n");
    fprintf(out, "\n");

    fprintf(out, "# HELP judge_verdict_ac_total AC verdicts\n");
    fprintf(out, "# TYPE judge_verdict_ac_total counter\n");
    fprintf(out, "judge_verdict_ac_total{contest=\"123\"} 890\n");
    // ... more metrics
}

// Run as HTTP server on :9090
// GET /metrics → export_metrics()
```

## Troubleshooting

### Judge host won't start
```bash
# Check KVM support
grep -c kvm /proc/cpuinfo  # Should be > 0

# Load modules
sudo modprobe kvm kvm_intel  # Intel
# or
sudo modprobe kvm kvm_amd    # AMD

# Check cgroup setup
mount | grep cgroup2
cat /sys/fs/cgroup/cgroup.controllers

# Verify initrd
file initrd_builder/build/initrd.img
```

### High latency
```
Normal: 0.5-1.0s per submission
Issue: >5s per submission

Diagnose:
1. Check CPU utilization: too many submissions?
2. Check cgroup throttling: cat /sys/fs/cgroup/judge/cpu.stat
3. Check memory pressure: free -h
4. Check I/O: iostat 1 10
5. Consider adding more judge hosts or splitting submissions
```

### Out of memory
```
Symptoms: Submissions killed with MLE, host OOM

Fix:
1. Reduce guest memory limit in judge_module_init()
2. Reduce concurrent submissions
3. Monitor peak_memory from verdicts table
4. Add more host RAM or split load across hosts
```

### Verdict mismatches
```
Symptoms: Different verdict locally vs. KVM

Diagnose:
1. Run test_judge_api (CPU-bound logic) → Should be identical
2. Check initrd gcc version matches host
3. Verify cgroup enforcement: cat memory.peak, cpu.stat
4. Compare execution environment variables
5. Check system time synchronization (NTP)
```

## Performance Tuning

### Latency Reduction
- Poll interval currently 100ms: reduce to 50ms for better latency
- Parallel judge hosts: each adds 1-2 submissions/sec
- Initrd size: larger initrd → slower boot (70MB baseline)

### Throughput Optimization
- Submissions per judge host: 1-2/sec sequential
- Parallel hosts: 4 hosts = 4-8 submissions/sec
- Database: batch-insert verdicts (10-100 per transaction)

### Resource Efficiency
- Memory per submission: ~500MB peak (200MB typical)
- CPU per submission: 0.1-1.0s user time
- Disk I/O: minimal (99% CPU-bound compilation/execution)

## Security Considerations

### Guest Isolation
- Each submission runs in isolated cgroup
- Memory/CPU limits enforced by kernel
- No network access from guest
- No privilege escalation possible

### Host Protection
- Executor runs as non-root user
- Cgroup safety net on host
- IPC via shared memory (no RPC attack surface)
- Seccomp BPF allows only essential syscalls

### Submission Validation
- Source code size limits (10MB default)
- Output size limits (10MB default)
- Timeout limits (5s default)
- Memory limits (256MB default)

## Example Integration: REST API

```python
# Flask API for submission evaluation
from flask import Flask, request, jsonify
import subprocess
import json

app = Flask(__name__)

@app.route('/submit', methods=['POST'])
def submit():
    data = request.json
    
    # Write submission to file
    with open(f'/tmp/{data["id"]}.c', 'w') as f:
        f.write(data['source'])
    
    # Call judge system
    result = subprocess.run([
        './build/simple_judge',
        f'/tmp/{data["id"]}.c',
        data['expected_output'],
        str(data['timeout'])
    ], capture_output=True, text=True)
    
    # Parse result
    verdict = json.loads(result.stdout)
    
    # Store in database
    # INSERT INTO verdicts ...
    
    return jsonify({
        'submission_id': data['id'],
        'verdict': verdict['verdict'],
        'cpu_time': verdict['cpu_time'],
        'memory': verdict['memory']
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
```

## Deployment Checklist

- [ ] Kernel 6.0+ with KVM and cgroup v2 enabled
- [ ] kvm-host built and tested
- [ ] initrd built with executor (test binary extraction)
- [ ] PostgreSQL schema created
- [ ] Redis queue configured
- [ ] Cgroup directories created with resource limits
- [ ] Judge host(s) started and healthy check passing
- [ ] Load balancer configured (if multi-host)
- [ ] Prometheus scrape config added
- [ ] Log aggregation set up (journalctl → ELK/Loki)
- [ ] Database backups configured
- [ ] Monitoring alerts configured
- [ ] Runbook documentation completed
- [ ] Team trained on debugging procedures

## References

- [KVM Documentation](https://www.kernel.org/doc/html/latest/virt/kvm/)
- [Cgroup v2 Interface](https://www.kernel.org/doc/html/latest/admin-guide/cgroup-v2.html)
- [Judge Module API](judge/examples/README.md)
- [Test Guides](judge/tests/KVM_INTEGRATION.md)
