/* getload() answers in CENTILOADS -- hundredths of a process wanting the
 * processor -- not in a floating point number of processes.  mgrload draws a
 * bar chart whose partitions are PSIZE=100 of whatever this returns, i.e.
 * 1.00 of a process to the partition, so a hundredth is two digits finer than
 * the graph can resolve, and this target's float support is incomplete.
 * Every backend converts to the same units; the ones that read a system load
 * average as text or as a double round it here.
 */
int getload();
