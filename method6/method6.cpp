// Lab5.cpp : Defines the entry point for the console application.
//

#include "windows.h"
#include "fileapi.h"

#include "stdio.h"
#include <cstdint>
#include <cassert>
#include <cstdlib>

DWORD Numbers[11] = {10, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

static const size_t chunkSize = 1024;

typedef struct task {
  HANDLE hFile;
  size_t offset;
  uint32_t crc;
  char buf[chunkSize];
  bool done;
} Task;

typedef struct _MY_OPERATION {
  OVERLAPPED Overlapped;
  task *task;
} MY_OPERATION, *PMY_OPERATION;

bool crc_table_computed = false;
unsigned char crc_table[256];
uint32_t crc_polynomial = 0xedb88320;

void compute_crc_table() {
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t j = 0, c = 0;
    for (c = i << 24, j = 8; j > 0; --j)
      c = c & 0x80000000 ? (c << 1) ^ crc_polynomial : (c << 1);
    crc_table[i] = c;
  }
}

uint32_t crc (uint32_t prev_crc, char* buf, size_t len) {
  if (!crc_table_computed) {
    compute_crc_table();
    crc_table_computed = true;
  }
  uint32_t result = prev_crc;
  for (size_t i = 0; i < len; ++i) {
    result = (result << 8) ^ crc_table[buf[i] ^ ((result) >> 24)];
  }
  return result;
}

bool Method5(HANDLE hFile, DWORD offset, DWORD len, Task *task) {
  DWORD cbRead = 0;
  auto pOverlapped = new _MY_OPERATION;
  memset(pOverlapped, 0, sizeof OVERLAPPED);
  pOverlapped->Overlapped.Offset = offset;
  pOverlapped->task = task;
  if (!ReadFile(hFile, &task->buf, len, nullptr, (LPOVERLAPPED) pOverlapped)) {
    auto err = GetLastError();
    if (ERROR_IO_PENDING != err) {
      printf("ReadFile failed with %lu \n", err);
      return false;
    }
  } else {
    printf(
        "ERROR: ReadFile returns TRUE instead of FALSE(ERROR_IO_PENDING) \n");
    return false;
  }
  return true;
}

VOID WINAPI MyCompletionRoutine(
	DWORD dwErrorCode,
	DWORD dwRead,
	LPOVERLAPPED lpOverlapped)
{
	const auto myOperation = (PMY_OPERATION)lpOverlapped;
	if (0 != dwErrorCode)
	{
		printf("Operation failed with %lu \n", dwErrorCode);
		goto out;
	}
  Task *task = myOperation->task;
  task->offset += cbRead;

  // Compute CRC
  task->crc =
      crc(task->crc, task->buf, cbRead);

  if (chunkSize == cbRead) {
    assert(Method5(task->hFile, task->offset, chunkSize, task));
  } else {
    task->done = true;
  }
out:
	delete myOperation;
}

int wmain(int argc, WCHAR *argv[]) {
  if (argc == 1) {
    printf("Usage: %s [files...]\n", argv[0]);
  }

  int task_count = argc - 1;

  // Open all the files and create corresponding task objects.
  auto tasks = new Task[argc - 1];
  for (int i = 1; i < argc; i++) {
    tasks[i - 1].hFile = CreateFile(
        argv[i], GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
    if (INVALID_HANDLE_VALUE == tasks[i-1].hFile) {
      printf("CreateFile failed with %lu \n", GetLastError());
      return 0;
    }
    tasks[i - 1].offset = 0;
    tasks[i - 1].crc = 0;
    tasks[i - 1].done = 0;
    memset(&tasks[i - 1].buf, 0, sizeof(tasks[i - 1].buf));
  }

	if (!BindIoCompletionCallback(hFile2, MyCompletionRoutine, 0))
	{
		printf("BindIoCompletionCallback failed with %lu \n", GetLastError());
		return 0;
	}

  // Create asyncronous read tasks.
  for (int i = 0; i < task_count; i++) {
    assert(Method5(tasks[i].hFile, 0, chunkSize, &tasks[i]));
  }

  do {
    Sleep(500);
    size_t tasks_done = 0;
    for (size_t i = 0; i < task_count; task_count++) {
      if (tasks[i].done) {
        tasks_done++;
      }
    }
  } while (tasks_done != task_count)

  uint32_t total_crc = 0;
  for (int i = 0; i < task_count; i++) {
    printf("Task %d: 0x%08x\n", i, tasks[i].crc);
    total_crc = crc(total_crc, (char*) &tasks[i].crc, sizeof(tasks[i].crc));
  }

  printf("Total checksum: 0x%08x\n", total_crc);
  return 0;
}

