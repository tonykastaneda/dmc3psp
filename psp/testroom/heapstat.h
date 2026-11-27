#ifndef HEAPSTAT_H
#define HEAPSTAT_H
/* Tests (build_testroom.ps1 -HeapStat): the heap's live bytes by call site, appended to HEAPSTAT.TXT with a label.
 * Without -HeapStat these do nothing. tools/heapstat_report.py turns the call sites into function names. */
void heapstat_dump(const char *path, const char *label);
#endif
