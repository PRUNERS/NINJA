#ifndef _NIN_FC_H_
#define _NIN_FC_H_

void ninj_fc_init(int argc, char **argv);
void ninj_fc_finalize();
void ninj_fc_get_delay(int dest, int tag, int comm_id, int *delay_flag, double *send_time);
void ninj_fc_report_send(size_t size_bytes, int tag, int comm_id);
void ninj_fc_report_recv();
void ninj_fc_do_model_tuning();
void ninj_fc_check_in_flight_msg(MPI_Comm comm);

#endif

