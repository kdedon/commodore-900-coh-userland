#include <sys/pstat.h>
    
struct pst_dynamic dynamic;

int getload(void)
{
  pstat(PSTAT_DYNAMIC,(union pstun)&dynamic,sizeof(dynamic),0,0);
  return ((int)(dynamic.psd_avg_1_min*100.0));
}
