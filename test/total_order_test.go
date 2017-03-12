package main

import (
	"bufio"
	"bytes"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"reflect"
	"strconv"
	"strings"
	"sync"
	"syscall"
	"testing"
)

const (
	executable = "../bin/proj2"
	firstPort  = 3456
)

// setupHostfile creates a temporary hostfile.
func setupHostfile(t *testing.T, numProcs int) string {
	f, err := ioutil.TempFile("", "hostfile")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	host, err := os.Hostname()
	if err != nil {
		t.Fatal(err)
	}

	for i := 0; i < numProcs; i++ {
		port := firstPort + i
		fmt.Fprintf(f, "%s:%d\n", host, port)
	}

	return f.Name()
}

// launchProcess launches a subprocess running ISIS Total Order Multicast Algorithm
// with the provided hostfile, number of messages, process id, and whether random
// delays should be introduced or not. It sends each message delivered to stdout on
// the message channel.
func launchProcess(
	hostfile string, numMessages, pid int, delays bool, msgc chan<- string,
) (kill func(), err error) {
	args := []string{
		"--hostfile", hostfile,
		"--count", strconv.Itoa(numMessages),
		"--id", strconv.Itoa(pid),
	}
	if delays {
		args = append(args, "--delays")
	}
	cmd := exec.Command(executable, args...)

	// Set up stderr and stdout streams.
	cmd.Stderr = os.Stderr
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return func() {}, err
	}

	// Return a function that kills the subprocess when called.
	cmd.SysProcAttr = &syscall.SysProcAttr{Setpgid: true}
	kill = func() {
		syscall.Kill(-cmd.Process.Pid, syscall.SIGKILL)
	}

	// Start the subprocess.
	if err := cmd.Start(); err != nil {
		return kill, err
	}

	// Scan in each message from the subprocess.
	scanner := bufio.NewScanner(stdout)
	for msgsLeft := numMessages; msgsLeft > 0 && scanner.Scan(); msgsLeft-- {
		msgc <- scanner.Text()
	}
	close(msgc)
	return kill, scanner.Err()
}

// stripProcFromMessage strips the process id prefix from the message line.
func stripProcFromMessage(t *testing.T, msgStr string) string {
	i := strings.Index(msgStr, ":")
	if i < 0 {
		t.Fatalf("message not formatted correctly: %q", msgStr)
	}
	return msgStr[i+2:]
}

func sumIntSlice(s []int) int {
	sum := 0
	for _, i := range s {
		sum += i
	}
	return sum
}

type msgOutput []string

func (m msgOutput) String() string {
	var b bytes.Buffer
	b.WriteString("[\n")
	for _, msg := range m {
		fmt.Fprintf(&b, " %s\n", msg)
	}
	b.WriteString("]\n")
	return b.String()
}

// testTotalOrder tests the ISIS Total Order Multicast Algorithm using the provided
// number of processes with their individually specified number of messages.
func testTotalOrder(t *testing.T, msgCounts []int, delays bool) {
	numProcs := len(msgCounts)
	hostfile := setupHostfile(t, numProcs)
	numMessages := sumIntSlice(msgCounts)

	// For each process, launch a goroutine that sends each delivered message
	// on its own buffered channel.
	var wg sync.WaitGroup
	errc := make(chan error, 1)
	msgChans := make([]chan string, numProcs)
	for i := 0; i < numProcs; i++ {
		msgc := make(chan string, numMessages)
		msgChans[i] = msgc

		wg.Add(1)
		go func(pid int) {
			kill, err := launchProcess(hostfile, numMessages, pid, delays, msgc)
			if err != nil {
				select {
				case errc <- err:
				default:
				}
			}

			// Don't kill our process until all processes have finished.
			wg.Done()
			wg.Wait()
			kill()
		}(i)
	}
	wg.Wait()

	// Make sure no process sent an error.
	if len(errc) > 0 {
		t.Fatal(<-errc)
	}

	// Read all messages from their channels.
	var msgsPerProc []msgOutput
	for _, msgChan := range msgChans {
		var msgs msgOutput
		for msg := range msgChan {
			t.Log(msg)
			msgs = append(msgs, stripProcFromMessage(t, msg))
		}
		msgsPerProc = append(msgsPerProc, msgs)
	}

	// Make sure all processes delivered the same messages in the same order.
	for i := 1; i < numProcs; i++ {
		msg0, msgI := msgsPerProc[0], msgsPerProc[i]
		if !reflect.DeepEqual(msg0, msgI) {
			t.Fatalf("messages delivered to process %d and process %d differ: \n%vvs.\n%v",
				0, i, msg0, msgI)
		}
	}
}

func testTotalOrderUniform(t *testing.T, delays bool) {
	testTotalOrder(t, []int{2, 2, 2, 2, 2}, delays)
}
func TestTotalOrderUniform(t *testing.T) {
	testTotalOrderUniform(t, false)
}
func TestTotalOrderUniformWithDelays(t *testing.T) {
	testTotalOrderUniform(t, true)
}

func testTotalOrderDifferent(t *testing.T, delays bool) {
	testTotalOrder(t, []int{0, 1, 2, 3, 4}, delays)
}
func TestTotalOrderDifferent(t *testing.T) {
	testTotalOrderDifferent(t, false)
}
func TestTotalOrderDifferentWithDelays(t *testing.T) {
	testTotalOrderDifferent(t, true)
}

func testTotalOrderSingleSender(t *testing.T, delays bool) {
	testTotalOrder(t, []int{100, 0, 0, 0, 0}, delays)
}
func TestTotalOrderSingleSender(t *testing.T) {
	testTotalOrderSingleSender(t, false)
}
func TestTotalOrderSingleSenderWithDelays(t *testing.T) {
	testTotalOrderSingleSender(t, true)
}

func testTotalOrderDualSenders(t *testing.T, delays bool) {
	testTotalOrder(t, []int{100, 100, 0, 0, 0}, delays)
}
func TestTotalOrderDualSenders(t *testing.T) {
	testTotalOrderDualSenders(t, false)
}
func TestTotalOrderDualSendersWithDelays(t *testing.T) {
	testTotalOrderDualSenders(t, true)
}

func testTotalOrderLarge(t *testing.T, delays bool) {
	testTotalOrder(t, []int{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}, delays)
}
func TestTotalOrderLarge(t *testing.T) {
	testTotalOrderLarge(t, false)
}
func TestTotalOrderLargeWithDelays(t *testing.T) {
	testTotalOrderLarge(t, true)
}
