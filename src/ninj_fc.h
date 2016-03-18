#ifndef _NIN_FC_H_
#define _NIN_FC_H_

void ninj_fc_init();
void ninj_fc_get_delay(int dest, int *delay_flag, double *send_time);
void ninj_fc_report_send(size_t size);


#endif

