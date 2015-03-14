#include <cnet.h>
#include <stdlib.h>
#include <string.h>
/*
Programmed by James Devito
Please note that this program is a heavily modified version
of stopandwait.c as provided by the cnet examples on the 
C313 homepage whick can be found in the labs section on
https://eclass.srv.ualberta.ca/course/view.php?id=21448

Some code and implementation inspired by TA help

I have collaborated some implementation ideas with
a colleage in the lab who I embarrassingly forgot to 
get the name of, we discussed trying to fix the corrupt frame
and bad argument errors and we discussed ways to implement a 
buffer
Please keep in mind that we only shared ideas and did not 
share raw code. This citation is necessary in the case of 
any similarities 
*/


/*  This is an implementation of a stop-and-wait data link protocol.
    It is based on Tanenbaum's `protocol 4', 2nd edition, p227
    (or his 3rd edition, p205).
    This protocol employs only data and acknowledgement frames -
    piggybacking and negative acknowledgements are not used.

    It is currently written so that only one node (number 0) will
    generate and transmit messages and the other (number 1) will receive
    them. This restriction seems to best demonstrate the protocol to
    those unfamiliar with it.
    The restriction can easily be removed by "commenting out" the line

	    if(nodeinfo.nodenumber == 0)

    in reboot_node(). Both nodes will then transmit and receive (why?).

    Note that this file only provides a reliable data-link layer for a
    network of 2 nodes.
 */

typedef enum    { DL_DATA, DL_ACK }   FRAMEKIND;

typedef struct {
    char        data[MAX_MESSAGE_SIZE];
} MSG;

typedef struct {
    FRAMEKIND    kind;      	// only ever DL_DATA or DL_ACK
    size_t	 len;       	// the length of the msg field only
    int          checksum;  	// checksum of the whole frame
    int          seq;       	// only ever 0 or 1
    MSG          msg;
} FRAME;

#define FRAME_HEADER_SIZE  (sizeof(FRAME) - sizeof(MSG))
#define FRAME_SIZE(f)      (FRAME_HEADER_SIZE + f.len)


static  MSG       	*lastmsg;
/*
  Implementing an idea attempting to fix corrupt frame error from
  https://eclass.srv.ualberta.ca/mod/forum/discuss.php?d=477474
 */
static size_t lastlength[5] = {0};
static  CnetTimerID	lasttimer		= NULLTIMER;

static  int       	ackexpected		= 0;
static	int		nextframetosend		= 0;
static	int		frameexpected		= 0;

//This is a lock that represents if a nodes buffer is full
static int bufFull = 0;


static void transmit_frame(MSG *msg, FRAMEKIND kind, size_t length, int seqno)
{
    FRAME       f;
    int link = 1;
    f.kind      = kind;
    f.seq       = seqno;
    f.checksum  = 0;
    f.len       = length;
    
   
    switch (kind) {
    case DL_ACK :
        printf("ACK transmitted, seq=%d, link=%d\n", seqno, link);
	break;

    case DL_DATA: {
      //If host send to link 1
      //If router send to link 2
      if (nodeinfo.nodetype == NT_HOST) {
	link = 1;
      } else { link = 2;}

	CnetTime	timeout;

        printf(" \nDATA transmitted, seq=%d, link=%d\n", seqno, link);
        memcpy(&f.msg, msg, (int)length);

	timeout = FRAME_SIZE(f)*((CnetTime)8000000 / linkinfo[link].bandwidth) +
				linkinfo[link].propagationdelay;

        lasttimer = CNET_start_timer(EV_TIMER1, 3 * timeout, 0);
	break;
      }
    }
    length      = FRAME_SIZE(f);
    f.checksum  = CNET_ccitt((unsigned char *)&f, (int)length);
    CHECK(CNET_write_physical(link, &f, &length));
}

static EVENT_HANDLER(application_ready)
{
    CnetAddr destaddr;

    lastlength[nodeinfo.nodenumber]  = sizeof(MSG);
    CHECK(CNET_read_application(&destaddr, &lastmsg[nodeinfo.nodenumber], &lastlength[nodeinfo.nodenumber]));
    CNET_disable_application(ALLNODES);

    printf("down from application, seq=%d\n", nextframetosend);
    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], nextframetosend);
    nextframetosend = 1-nextframetosend;
}

static EVENT_HANDLER(physical_ready)
{
    FRAME        f;
    size_t	 len;
    int          link, checksum;

    len         = sizeof(FRAME);
    CHECK(CNET_read_physical(&link, &f, &len));

    checksum    = f.checksum;
    f.checksum  = 0;
    if(CNET_ccitt((unsigned char *)&f, (int)len) != checksum) {
        printf("\t\t\t\tBAD checksum - frame ignored\n");
        return;           // bad checksum, ignore frame
    }
    //Do not need to separate physical layer code by type
    //ie: NT_HOST/NT_ROUTER

    switch (f.kind) {
      //Handle when we get an ACK
    case DL_ACK :
        if(f.seq == ackexpected) {
            printf("\t\t\t\tACK received, seq=%d\n", f.seq);
            CNET_stop_timer(lasttimer);
            ackexpected = 1-ackexpected;
	    //Got ack back, So we can send our next DATA
            bufFull = 0;
            if(nodeinfo.nodenumber == 0){
                CNET_enable_application(ALLNODES);
            }
        }
	break;
	//Handle when we get DATA
    case DL_DATA :
      
        printf("\t\t\t\tDATA received, seq=%d, ", f.seq);
        if(f.seq == frameexpected) {
	  /*
	    End host doesn't need to transmit a frame on data receive
	    because we are finished
	    In a more complete implementation we would instead check if 
	    the received data is at its destination node
	  */
            if(nodeinfo.nodenumber == 4){
                printf("up to application\n");
                frameexpected = 1-frameexpected;
                len = f.len;
                CHECK(CNET_write_application(&f.msg, &len));
            } else {
	      //Don't send if our buffer is busy
                if(bufFull){
                    printf("Ignore - Buffer Full :  seq %d.\n", f.seq);
                    return;
                } else {
		  //We are sending so flip our buffer "lock" to full 
		    bufFull= 1;
                    frameexpected = 1-frameexpected;
                    lastlength[nodeinfo.nodenumber] = f.len;
                    lastmsg[nodeinfo.nodenumber] = f.msg;
                    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], ackexpected);
                }
            }
        }
        else
            printf("ignored (seq num)\n");
        transmit_frame(NULL, DL_ACK, 0, f.seq);
	break;
    }
}

static EVENT_HANDLER(timeouts)
{
    printf("timeout, seq=%d\n", ackexpected);
    transmit_frame(&lastmsg[nodeinfo.nodenumber], DL_DATA, lastlength[nodeinfo.nodenumber], ackexpected);
}

static EVENT_HANDLER(showstate)
{
    printf(
    "\n\tackexpected\t= %d\n\tnextframetosend\t= %d\n\tframeexpected\t= %d\n",
		    ackexpected, nextframetosend, frameexpected);
}

EVENT_HANDLER(reboot_node)
{
  /*
    This is an attempt to fix corrupt frame errors
    This idea has come from
    https://eclass.srv.ualberta.ca/mod/forum/discuss.php?d=477474
  */
    lastmsg = calloc(5, sizeof(MSG));

    //Sender needs this event
    if(nodeinfo.nodenumber == 0)
        CHECK(CNET_set_handler( EV_APPLICATIONREADY, application_ready, 0));

    CHECK(CNET_set_handler( EV_PHYSICALREADY,    physical_ready, 0));
    CHECK(CNET_set_handler( EV_TIMER1,           timeouts, 0));
    CHECK(CNET_set_handler( EV_DEBUG0,           showstate, 0));

    CHECK(CNET_set_debug_string( EV_DEBUG0, "State"));

    if(nodeinfo.nodenumber == 0)
        CNET_enable_application(ALLNODES);
}
