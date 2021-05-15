/*	##############################################################################################
 *      Advanced RTI System, ARTÌS			http://pads.cs.unibo.it
 *      Large Unstructured NEtwork Simulator (LUNES)
 *
 *      Description:
 *              -	In this file are defined the message types and their structures
 *
 *      Authors:
 *              First version by Gabriele D'Angelo <g.dangelo@unibo.it>
 *
 ############################################################################################### */

#ifndef __MESSAGE_DEFINITION_H
#define __MESSAGE_DEFINITION_H

#include "entity_definition.h"

/*---- M E S S A G E S    D E F I N I T I O N ---------------------------------*/

// Model messages definition
typedef struct _request_msg    RequestMsg; // Interactions among messages
typedef struct _link_msg       LinkMsg;  // Network constructions
typedef struct _unlink_msg     UnlinkMsg;  // Network constructions
typedef struct _migr_msg       MigrMsg;  // Migration message
typedef union   msg            Msg;

// General note:
//	each type of message is composed of a static and a dynamic part
//	-	the static part contains a pre-defined set of variables, and the size
//		of the dynamic part (as number of records)
//	-	a dynamic part that is composed of a sequence of records


struct _request_static_part {
    char           type;                    // Message type
    float          timestamp;               // Timestep of creation (of the message)
    unsigned short ttl;                     // Time-To-Live
    int            id;                 	    // Message Identifier
    unsigned int   creator;                 // ID of the original sender of the message
   // #ifdef DEGREE_DEPENDENT_GOSSIP_SUPPORT
    unsigned int   num_neighbors;           // Number of neighbors of forwarder
    //#endif
};
//

struct _request_msg {
    struct  _request_static_part request_static;
};

// **********************************************
// LINK MESSAGES
// **********************************************
/*! \brief Record definition for dynamic part of link messages */
struct _link_record {
    unsigned int key;
    unsigned int value;
};
//
/*! \brief Static part of link messages */
struct _link_static_part {
    char type; // Message type
};
//
/*! \brief Link message */
struct _link_msg {
    struct  _link_static_part link_static; // Static part
};

/*! \brief Record definition for dynamic part of unlink messages */
struct _unlink_record {
    unsigned int key;
    unsigned int value;
};
//
/*! \brief Static part of unlink messages */
struct _unlink_static_part {
    char type; // Message type
};
//
/*! \brief unLink message */
struct _unlink_msg {
    struct  _unlink_static_part unlink_static; // Static part
};


// **********************************************
// MIGRATION MESSAGES
// **********************************************
//
/*! \brief Static part of migration messages */
struct _migration_static_part {
    char          type;        // Message type
    unsigned int  dyn_records; // Number of records in the dynamic part of the message
};
//
/*! \brief Dynamic part of migration messages */
struct _migration_dynamic_part {
    struct state_element records[MAX_MIGRATION_DYNAMIC_RECORDS]; // It is an array of records
};
//
/*! \brief Migration message */
struct _migr_msg {
    struct  _migration_static_part  migration_static;  // Static part
    struct  _migration_dynamic_part migration_dynamic; // Dynamic part
};



/*! \brief Union structure for all types of messages */
union msg {
    char       type;
    LinkMsg    link;
    RequestMsg request;
    MigrMsg    migr;
    UnlinkMsg  unlink;
};
/*---------------------------------------------------------------------------*/

#endif /* __MESSAGE_DEFINITION_H */
