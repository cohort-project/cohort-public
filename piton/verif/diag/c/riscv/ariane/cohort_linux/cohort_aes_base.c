#include <stdio.h>
#include "util.h"

#ifndef FIFO_SIZE 
#define FIFO_SIZE 64
#endif

#ifndef WAIT_COUNTER_VAL 
#define WAIT_COUNTER_VAL 16
#endif

#ifndef SERIALIZATION_VAL
#define SERIALIZATION_VAL 1
#endif

#ifndef DESERIALIZATION_VAL
#define DESERIALIZATION_VAL 1
#endif

#ifndef BACKOFF_COUNTER_VAL
#define BACKOFF_COUNTER_VAL 240
#endif

#ifndef LOOP_NUM
#define LOOP_NUM 20
#endif

#ifndef BATCH_SIZE
#define BATCH_SIZE 8
#endif

#ifdef BATCH_SIZE
#define OUT_BATCH_SIZE BATCH_SIZE
// divide by 2 every BATCH_SIZE to check SHA
#else
#define OUT_BATCH_SIZE BATCH_SIZE/2
#endif

#ifdef BARE_METAL
#define NO_DRIVER
#endif

#ifdef NO_DRIVER
    #include "cohort.h"
#else
    #include "cohort_uapi.h"
#endif

#define NUM_WORDS 32

extern uint64_t back_off_count;

static uint64_t plain[NUM_WORDS] =  {
0xFFFFFFFFFFFFFFFFULL,0x0000000033221100ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000077665544ULL,
0xFFFFFFFFFFFFFFFFULL,0x00000000BBAA9988ULL,
0xFFFFFFFFFFFFFFFFULL,0x00000000FFEEDDCCULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000011111111ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000022222222ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000033333333ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000044444444ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000055555555ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000066666666ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000077777777ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000088888888ULL,
0xFFFFFFFFFFFFFFFFULL,0x0000000099999999ULL,
0xFFFFFFFFFFFFFFFFULL,0x00000000AAAAAAAAULL,
0xFFFFFFFFFFFFFFFFULL,0x00000000BBBBBBBBULL,
0xFFFFFFFFFFFFFFFFULL,0x00000000CCCCCCCCULL};

static uint64_t A,D;
static uint32_t Dp[2] = {0x000000FF,0x000000AA};
static uint32_t Ap[NUM_WORDS/2] = {0x33221100,
                                                 0x77665544,
                                                 0xBBAA9988,
                                                 0xFFEEDDCC,
                                                 0x11111111,
                                                 0x22222222,
                                                 0x33333333,
                                                 0x44444444,
                                                 0x55555555,
                                                 0x66666666,
                                                 0x77777777,
                                                 0x88888888,
                                                 0x99999999,
                                                 0xAAAAAAAA,
                                                 0xBBBBBBBB,
                                                 0xCCCCCCCC};


#ifndef NUM_A
    #define NUM_A 1
#endif

void _kernel_(uint32_t id, uint32_t core_num){
    dec_open_producer(id);
    dec_open_consumer(id);
    // Enable Virtual Addr Translation
    dec_set_tlb_ptbase(0,0);

    A = (uint64_t)Ap | 0x7F00000000LL;
    D = (uint64_t)Dp | 0x7F00000000LL;

    uint64_t ppn = ((uint64_t)Ap >> 12);
    uint64_t vpn = ((uint64_t)A >> 12);
    print64("virtual page number", vpn);
    uint64_t mmpage = 0x0LL;
    mmpage |= ppn << 4; // [31:4] PPN
    mmpage |= vpn << 32; // [58:32] VPN
    // [63:62] is page size (1G,2M), flag bits are all 1's
    print64("Set TLB entry",mmpage);
    dec_set_tlb_mmpage(0, mmpage);

    //print64("Get TLB fault",dec_get_tlb_fault(0) );
    for (int i=0;i<1;i++){
        // Lower 4 bits {dirty bit, is_2M,is_1G,valid}
        // [31:4] is PPN
        // [59:32] is Asid, VPN
        // [63:60] Flag bits
        //print64("Get TLB entry",dec_snoop_tlb_entry(0) );
    }

    dec_set_base32(id,(void *)A);
    for (int i=0;i<4;i++){
        //dec_produce32(id,42);
        //dec_load32_asynci_llc(id,i%2);
        dec_atomic_fetch_add_asynci(id,i%2,1);
        __sync_synchronize;
        print32("A +1",dec_consume32(id));

        dec_set_base32(id,(void *)D);
        dec_atomic_compare_exchange_asynci(id,i%2, Dp[i%2], Dp[i%2]+1);
        print32("D CAS(+1)",dec_consume32(id));

        dec_set_base32(id,(void *)A);
        dec_atomic_compare_exchange_asynci(id,i%2, Ap[i%2], Ap[i%2]+1);
        print32("A CAS(+1)",dec_consume32(id));

        dec_load32_asynci(id,i%2);
        print32("TLoad A",dec_consume32(id));

        //__sync_synchronize; FENCE;
        //if (data!=A[0]) return 1;
    }
}

int cohort_set_tlb(uint64_t vpn, uint64_t ppn)
{
//    print64("virtual page number", vpn);
//    print64("physical page number", ppn);
    uint64_t mmpage = 0x0ULL;
    mmpage |= ppn << 4; // [31:4] PPN
    mmpage |= vpn << 32; // [58:32] VPN
    // [63:62] is page size (1G,2M), flag bits are all 1's
//    print64("Set TLB entry",mmpage);
    dec_set_tlb_mmpage(0, mmpage);
//    print64("Get TLB entry",dec_snoop_tlb_entry(0) );
}

int main(int argc, char ** argv) {

    volatile static uint32_t amo_cnt = 0;
    uint32_t id, core_num;
#ifdef BARE_METAL
    id = argv[0][0];
    core_num = argv[0][1];
#else
    id = 0;
    core_num = 1;
#endif
    // only make the first ariane initialize the tile
    if (id == 0) init_tile(NUM_A);
    if (id == 1) while(true);

// #ifdef BARE_METAL
//     cohort_set_tlb(0x80004, 0x80004);
//     cohort_set_tlb(0x80005, 0x80005);
// #endif

#ifdef NO_DRIVER
    const c_id_t c_id = 0;

    unsigned long long int write_value = 11;
    unsigned long long int serialization_value = 1;
    unsigned long long int deserialization_value = 1;
    unsigned long long int wait_counter = WAIT_COUNTER_VAL;
    unsigned long long int backoff_counter = BACKOFF_COUNTER_VAL;

    write_value |= backoff_counter << 48;
    write_value |= serialization_value << 32;
    write_value |= deserialization_value << 16;
    write_value |= wait_counter << 4;
#else
    uint8_t c_id = 0;
#endif

	uint64_t accumulator = 0;
   
    // 32 bits elements, fifo length = 8
    fifo_ctrl_t *sw_to_cohort_fifo = fifo_init( FIFO_SIZE+1, 64, 0, c_id);
    fifo_ctrl_t *cohort_to_sw_fifo = fifo_init( FIFO_SIZE+1, 64, 1, c_id);
    void *acc_address = memalign(128, 128);
    memset(acc_address, 0, 128);

	uint64_t element;

#ifdef NO_DRIVER
    baremetal_write(0, 6, (uint64_t) acc_address);

    cohort_on(c_id);

    baremetal_write(0, 7, write_value);
#else
    int beg_status = syscall(258, (uint64_t) sw_to_cohort_fifo->head_ptr, (uint64_t) cohort_to_sw_fifo->head_ptr, (uint64_t) acc_address, BACKOFF_COUNTER_VAL);
#endif

    unsigned long _c = -rdcycle();
    unsigned long _i = -rdinstret();

	dec_open_producer(0);
	dec_open_consumer(0);

		for (int k = 0; k < FIFO_SIZE/BATCH_SIZE; k++) {
			for (int j = 0; j < BATCH_SIZE; j++) {
                // memcpy(sw_to_cohort_fifo->data_array + ((k*BATCH_SIZE)), plain + (((k*BATCH_SIZE)%NUM_WORDS)), BATCH_SIZE*8);
				// fifo_push_64(k*BATCH_SIZE + j, sw_to_cohort_fifo, k*BATCH_SIZE + j);
				fifo_push_64(plain[(k*BATCH_SIZE + j)%NUM_WORDS], sw_to_cohort_fifo, k*BATCH_SIZE + j);
			}
			fifo_push_sync(sw_to_cohort_fifo, k*BATCH_SIZE + BATCH_SIZE);
            
            // divide by 2 every BATCH_SIZE to check SHA
			for (int j = 0; j < OUT_BATCH_SIZE; j++) {
				element = fifo_pop_64(cohort_to_sw_fifo, k*(OUT_BATCH_SIZE)+j);
				//printf("e: %llx\n", element);
				accumulator += element;
			}
			fifo_pop_sync(cohort_to_sw_fifo, k*(OUT_BATCH_SIZE) + (OUT_BATCH_SIZE));
		}

	dec_close_producer(0);
	dec_close_consumer(0);
    
    _c += rdcycle();
    _i += rdinstret();

    print_stats_fifos(1);

    printf("%ld, %ld, %ld.%ld, %ld.%ld, ", _c, _i, _c/_i, 1000*_c/_i%1000, _i/_c, 1000*_i/_c%1000);
    printf("%d, %d, %d, %d, %d, ", LOOP_NUM, BACKOFF_COUNTER_VAL, WAIT_COUNTER_VAL, FIFO_SIZE, BATCH_SIZE);

#ifdef NO_DRIVER
    cohort_off(c_id);
#else
    int status = syscall(257);
#endif

    fifo_deinit(sw_to_cohort_fifo);
    fifo_deinit(cohort_to_sw_fifo);
    free(acc_address);
 	//fifo_dump(cohort_to_sw_fifo);
	printf("accumulator is %llx, %d\n", accumulator, back_off_count);

	return 0;
}