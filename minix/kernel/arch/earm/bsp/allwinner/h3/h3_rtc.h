#ifndef __H3_RTC_H
#define __H3_RTC_H

#define H3_RTC_BASE 0x01F00000
#define H3_RTC_SIZE 0x1000


void h3_rtc_init(void);
void h3_rtc_run(void);

#endif /* __H3_RTC_H */
