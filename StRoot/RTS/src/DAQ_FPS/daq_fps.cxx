#include <sys/types.h>
#include <errno.h>
#include <assert.h>
#include <stdlib.h>

#include <rtsLog.h>
#include <rtsSystems.h>



#include <SFS/sfs_index.h>
#include <DAQ_READER/daqReader.h>
#include <DAQ_READER/daq_dta.h>


#include "daq_fps.h"


const char *daq_fps::help_string = "FPS\n\
raw	returns raw data\n" ;

class daq_det_fps_factory : public daq_det_factory
{
public:
	daq_det_fps_factory() {
		daq_det_factory::det_factories[FPS_ID] = this ;
	}

	daq_det *create() {
		return new daq_fps ;
	}
} ;

static daq_det_fps_factory fps_factory ;


daq_fps::daq_fps(daqReader *rts_caller) 
{
	rts_id = FPS_ID ;
	name = rts2name(rts_id) ;
	sfs_name = "fps" ;
	caller = rts_caller ;
	
	if(caller) caller->insert(this, rts_id) ;

	raw = new daq_dta ;
	adc = new daq_dta ;
	pedrms = new daq_dta ;

	LOG(DBG,"%s: constructor: caller %p",name,rts_caller) ;
	return ;
}

daq_fps::~daq_fps() 
{
	LOG(DBG,"%s: DEstructor",name) ;

	delete raw ;
	delete adc ;
	delete pedrms ;

	return ;
}



daq_dta *daq_fps::get(const char *bank, int sec, int rdo, int pad, void *p1, void *p2) 
{	
	Make() ;

	if(present == 0) return 0 ;


	if(strcasecmp(bank,"raw")==0) {
		return handle_raw() ;
	}
	else if(strcasecmp(bank,"adc")==0) {
		return handle_adc() ;
	}
	else if(strcasecmp(bank,"pedrms")==0) {
		return handle_pedrms() ;
	}


	LOG(ERR,"%s: unknown bank type \"%s\"",name,bank) ;
	return 0 ;
}




daq_dta *daq_fps::handle_adc()
{

	daq_dta *raw_d ;


	raw_d = handle_raw() ;
	if(raw_d==0) return 0 ;
	if(raw_d->iterate() == 0) return 0 ;


	fps_evt_hdr_t *hdr = (fps_evt_hdr_t *) raw_d->Void ;
	u_int *d32 = (u_int *) raw_d->Void ;

	adc->create(32,"fps_adc",rts_id,DAQ_DTA_STRUCT(fps_adc_t)) ;

	int tb_cou = hdr->pre_post_cou ;
	int qt_cou = hdr->qt_cou ;

	//LOG(TERR,"tb %d, qt %d, hdr ver 0x%08X",tb_cou,qt_cou,hdr->ver) ;

	memcpy(&meta_hdr,hdr,sizeof(meta_hdr)) ;
	adc->meta = (void *) &meta_hdr ;

	d32 += (hdr->ver & 0xFF) ;	// skip to data...


	
	for(int tb=0;tb<tb_cou;tb++) {
		int rel_xing = *d32++ ;

		//LOG(TERR,"Rel xing %d",rel_xing) ;
		
		for(int q=0;q<qt_cou;q++) {
			int qt = *d32++ ;
			int chs = *d32++ ;
	
			//LOG(TERR,"qt %d, chs %d",qt,chs) ;

			if(chs==0) continue ;

			fps_adc_t *a = (fps_adc_t *) adc->request(chs) ;

			//LOG(TERR,"TB %d, QT %d, chs %d",tb,qt,chs) ;
			
			for(int c=0;c<chs;c++) {
				u_int datum = *d32++ ;

				int ch = datum >> 27 ;
				int aadc = datum & 0x0FFF ;
				int tdc = (datum >> 16) & 0x07FF ;

				a->ch = ch ;
				a->adc = aadc ;
				a->tdc = tdc ;
				a++ ;
			}

			adc->finalize(chs,rel_xing,qt,0) ;
		}
	}



	adc->rewind() ;

	return adc ;
	
}


daq_dta *daq_fps::handle_raw()
{
	char *st ;
	int bytes ;
	char str[256] ;
	char *full_name ;


	assert(caller) ;	// sanity...

	if(!present) {
		return 0 ;
	}
	else {
		LOG(DBG,"%s: present %d",name,present) ;
	}


	raw->create(1024,"fps_raw",rts_id,DAQ_DTA_STRUCT(char)) ;


	sprintf(str,"%s/sec01/rb01/raw",sfs_name) ;
	full_name = caller->get_sfs_name(str) ;
		
	//LOG(TERR,"got full name") ;

	if(!full_name) return 0 ;
	bytes = caller->sfs->fileSize(full_name) ;	// this is bytes

	st = (char *) raw->request(bytes) ;
		
	int ret = caller->sfs->read(str, st, bytes) ;
	if(ret != bytes) {
		LOG(ERR,"ret is %d") ;
	}

	//LOG(TERR,"got bytes",bytes) ;
	
	raw->finalize(bytes,0,0,0) ;	;

	raw->rewind() ;

	return raw ;
	
}

	

daq_dta *daq_fps::handle_pedrms()
{
	char str[128] ;
	char *full_name ;
	int bytes ;


	sprintf(str,"%s/sec%02d/pedrms",sfs_name, 1) ;

	LOG(NOTE,"Trying %s",str) ;

	full_name = caller->get_sfs_name(str) ;
		
	if(full_name) {
		LOG(NOTE,"full_name %s",full_name) ;
	}

	if(!full_name) return 0 ;  ;


	bytes = caller->sfs->fileSize(full_name) ;	// this is bytes

	LOG(NOTE,"bytes %d",bytes) ;

	int nitems = bytes / sizeof(fps_pedrms_t) ;
	int remain = bytes % sizeof(fps_pedrms_t) ;

	if(remain) {
		LOG(ERR,"Got %d, expect %d",bytes,sizeof(fps_pedrms_t)) ;
		return 0 ;
	}

	char *data = (char *)malloc(bytes) ;

	int ret = caller->sfs->read(str, (char *)data, bytes) ;
	if(ret != bytes) {
		LOG(ERR,"ret is %d") ;
	}

	pedrms->create(1,"fps_pedrms",rts_id,DAQ_DTA_STRUCT(fps_pedrms_t)) ;

	for(int i=0;i<nitems;i++) {

		fps_pedrms_t *ped = (fps_pedrms_t *)pedrms->request(1) ;

		memcpy(ped,data+i*sizeof(fps_pedrms_t),sizeof(fps_pedrms_t)) ;

		if(ped->version != FPS_PED_VERSION) {
			LOG(ERR,"Wrong version %d in file, expect %d",ped->version,FPS_PED_VERSION) ;
		}

		pedrms->finalize(1,1,ped->qt_ix,0) ;
	}

	free(data) ;

	pedrms->rewind() ;

	return pedrms ;
}

/*
	Checks the STP trigger contribution and extracts the token
*/

int daq_fps::get_l2(char *addr, int words, struct daq_trg_word *trg, int rdo)
{
	int t_cou = 0 ;
	int err = 0 ;

	fps_evt_hdr_t *h = (fps_evt_hdr_t *) addr ;



	trg[t_cou].trg = 4 ;
	trg[t_cou].daq = 2 ;
	trg[t_cou].rhic = h->tick ;
	trg[t_cou].rhic_delta = 0 ;
	trg[t_cou].t = h->token;
	t_cou++ ;

	if(h->status == 0) {
		err |= 1 ;
	}

	if(err) {
		LOG(ERR,"%u: bad QT event 0x%08X 0x%08X 0x%08X [%d]",h->ev,h->stp_data[0],h->stp_data[1],h->stp_data[2],err) ;
		return -1 ;
	}

	return t_cou ;
}

