/*	##############################################################################################
 *      Advanced RTI System, ARTÌS			http://pads.cs.unibo.it
 *      Large Unstructured NEtwork Simulator (LUNES)
 *
 *      Description:
 *              -	In this file you find all the user event handlers to be used to implement a
 *                      discrete event simulation. Only the modelling part is to be inserted in this
 *                      file, other tasks such as GAIA-related data structure management are
 *                      implemented in other parts of the code.
 *              -	Some "support" functions are also present.
 *              -	This file is part of the MIGRATION-AGENTS template provided in the
 *                      ARTÌS/GAIA software distribution but some modifications have been done to
 *                      include the LUNES features.
 *
 *      Authors:
 *              First version by Gabriele D'Angelo <g.dangelo@unibo.it>
 *
 ############################################################################################### */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <ctype.h>
#include <assert.h>
#include <ini.h>
#include <ts.h>
#include <rnd.h>
#include <gaia.h>
#include <rnd.h>
#include "utils.h"
#include "msg_definition.h"
#include "lunes.h"
#include "lunes_constants.h"
#include "user_event_handlers.h"


/* ************************************************************************ */
/*          E X T E R N A L     V A R I A B L E S                           */
/* ************************************************************************ */

extern hash_t hash_table, *table;                   /* Global hash table of simulated entities */
extern hash_t sim_table, *stable;                   /* Hash table of locally simulated entities */
extern double simclock;                             /* Time management, simulated time */
extern TSeed  Seed, *S;                             /* Seed used for the random generator */
extern FILE * fp_print_trace;                       /* File descriptor for simulation trace file */
extern char * TESTNAME;                             /* Test name */
extern int    LPID;                                 /* Identification number of the local Logical Process */
extern int    local_pid;                            /* Process identifier */
extern int    NSIMULATE;                            /* Number of Interacting Agents (Simulated Entities) per LP */
extern int    NLP;                                  /* Number of Logical Processes */
// Simulation control
extern unsigned int   env_migration;                /* Migration state */
extern float          env_migration_factor;         /* Migration factor */
extern unsigned int   env_load;                     /* Load balancing */
extern float          env_end_clock;                /* End clock (simulated time) */
extern unsigned short env_max_ttl;                  /* TTL of new messages */
extern unsigned short env_dissemination_mode;       /* Dissemination mode */
extern float          env_broadcast_prob_threshold; /* Dissemination: conditional broadcast, probability threshold */
extern float          env_dandelion_stem_steps;    /* Dissemination: number of steps of fluff and stem phase*/
extern unsigned int   env_cache_size;               /* Cache size of each node */
extern float          env_fixed_prob_threshold;     /* Dissemination: fixed probability, probability threshold */
extern int 			  env_perc_active_nodes_;		/* Initial percentage of active node*/
extern unsigned int   env_probability_function;     /* Probability function for Degree Dependent Gossip */
extern double         env_function_coefficient;     /* Coefficient of probability function */
extern int            applicant;                    /* ID of the applicant node*/
extern int            holder;                       /* ID of the holder node*/



/* ************************************************************************ */
/*       S U P P O R T     F U N C T I O N S			                    */
/* ************************************************************************ */

/* ***************************** D E B U G **********************************/

/*! \brief Prints out the whole content of a glib hashtable data structure
 */
void UNUSED hash_table_print(GHashTable *ht) {
    // Iterator to scan the whole state hashtable of entities
    GHashTableIter iter;
    gpointer       key, value;

    g_hash_table_iter_init(&iter, ht);

    while (g_hash_table_iter_next(&iter, &key, &value)) {
        fprintf(stdout, "DEBUG: %d:%d\n", *(unsigned int *)key, *(unsigned int *)value);
        fflush(stdout);
    }
}

/*! \brief Returns a random key from a hash table
 */
gpointer  hash_table_random_key(GHashTable *ht) {
    // Iterator to scan the (whole) state hashtable of entities
    GHashTableIter iter;
    gpointer       key, value;
    guint          size;
    unsigned int   position;

    size     = g_hash_table_size(ht);
    position = RND_Integer(S, (double)1, (double)size);

    g_hash_table_iter_init(&iter, ht);

    while (position) {
        g_hash_table_iter_next(&iter, &key, &value);
        position--;
    }
    return(key);
}


/*! \brief Utility to check environment variables, if the variable is not defined then the run is aborted
 */
char *check_and_getenv(char *variable) {
    char *value;

    value = getenv(variable);
    if (value == NULL) {
        fprintf(stdout, "The environment variable %s is not defined\n", variable);
        fflush(stdout);
        exit(1);
    }else {
        return(value);
    }
}

/* *********** E N T I T Y    S T A T E    M A N A G E M E N T **************/

/*! \brief Adds a new entry in the hash table that implements the SE's local state
 *         Note: it is used both from the register and the migration handles
 */
int add_entity_state_entry(unsigned int key, value_element *val, int id, hash_node_t *node) {
    struct  state_element *state_e;

    // First of all, it is necessary to check if the used key is already in the hash table
    if (g_hash_table_lookup(node->data->state, &key) != NULL) { return(-1); }

    // The number of state records is limited by the MAX_MIGRATION_DYNAMIC_RECORDS constant,
    //	that is the max number of records that can be inserted in a migration message
    if (g_hash_table_size(node->data->state) > MAX_MIGRATION_DYNAMIC_RECORDS) {
        // No more entries can be added, the resulting state would be impossible to migrate
        fprintf(stdout, "%12.2f node: FATAL ERROR, [%5d] impossible to add new elements to the state hash table of this node, see constant MAX_MIGRATION_DYNAMIC_RECORDS in file: sim-parameters.h\n", simclock, id);
        fflush(stdout);
        exit(-1);
    }

    // Dynamic allocation of memory and initialization of values
    // Note: this memory will be automatically freed in case of SE migration
    state_e = g_malloc(sizeof(struct state_element));
    if (state_e) {
        state_e->key      = key;
        state_e->elements = *val;

        g_hash_table_insert(node->data->state, &(state_e->key), &(state_e->elements));

        #ifdef DEBUG
        fprintf(stdout, "%12.2f node: [%5d] local state key: %d, local hash_size: %d\n", simclock, id, state_e->key, g_hash_table_size(node->data->state));
        fflush(stdout);
        #endif
        return(1);
    }else {
        // Unable to allocate memory for state elements
        fprintf(stdout, "%12.2f node: FATAL ERROR, [%5d], memory allocation, impossible to add new elements to the state hash table of this node\n", simclock, id);
        fflush(stdout);
        exit(-1);
    }
}

/*! \brief Deletes an entry in the hash table that implements the SE's local state
 *         Note: the freeing of the associated memory is automatic
 */
int delete_entity_state_entry(unsigned int key, hash_node_t *node) {
    if (g_hash_table_remove(node->data->state, &key) == TRUE) { return(0); }else {
        return(-1);
    }
}

/*! \brief Modifies the value of an entry in the SE's local state
 */
int modify_entity_state_entry(unsigned int key, unsigned int new_value, hash_node_t *node) {
    unsigned int *value;

    value = g_hash_table_lookup(node->data->state, &key);

    if (value) {
        *(value) = new_value;
        return(0);
    }else {
        return(-1);
    }
}


void execute_request(double ts, hash_node_t *src, hash_node_t *dest, unsigned short ttl, int req_id, float timestamp, unsigned int creator) {
    RequestMsg     msg;
    unsigned int message_size;

    // Defining the message type
    msg.request_static.type = 'R';

    msg.request_static.timestamp  = timestamp;
    msg.request_static.ttl        = ttl;
    msg.request_static.creator    = creator;
    message_size = sizeof(struct _request_static_part);

    // Buffer check
    if (message_size > BUFFER_SIZE) {
        fprintf(stdout, "%12.2f FATAL ERROR, the outgoing BUFFER_SIZE is not sufficient!\n", simclock);
        fflush(stdout);
        exit(-1);
    }

    if (ttl > 0){
        GAIA_Send(src->data->key, dest->data->key, ts, (void *)&msg, message_size);
    }
    // Real send

}


/*! \brief Links another SE, creating and sending a 'L' type message
 *         In LUNES it is used to build up the graph structure that has been read
 *         from the input graph definition file (in dot format).
 */
void execute_link(double ts, hash_node_t *src, hash_node_t *dest) {
    LinkMsg      msg;
    unsigned int message_size;

    // Defining the message type
    msg.link_static.type = 'L';

    // To reduce the network overhead, only the used part of the message is really sent
    message_size = sizeof(struct _link_static_part);

    // Buffer check
    if (message_size > BUFFER_SIZE) {
        fprintf(stdout, "%12.2f FATAL ERROR, the outgoing BUFFER_SIZE is not sufficient!\n", simclock);
        fflush(stdout);
        exit(-1);
    }

    // Real send
    GAIA_Send(src->data->key, dest->data->key, ts, (void *)&msg, message_size);
}

void execute_unlink(double ts, hash_node_t *src, hash_node_t *dest) {
    UnlinkMsg      msg;
    unsigned int message_size;

    // Defining the message type
    msg.unlink_static.type = 'U';

    // To reduce the network overhead, only the used part of the message is really sent
    message_size = sizeof(struct _unlink_static_part);

    // Buffer check
    if (message_size > BUFFER_SIZE) {
        fprintf(stdout, "%12.2f FATAL ERROR, the outgoing BUFFER_SIZE is not sufficient!\n", simclock);
        fflush(stdout);
        exit(-1);
    }

    // Real send
    GAIA_Send(src->data->key, dest->data->key, ts, (void *)&msg, message_size);
}

/* ************************************************************************ */
/*      U S E R   E V E N T   H A N D L E R S			                    */
/*									                                        */
/*	NOTE: when a handler required extensive modifications for LUNES	        */
/*		then it calls another user level handerl called		                */
/*		lunes_<handler_name> and placed in the file lunes.c	                */
/* ************************************************************************ */


/****************************************************************************
 *! \brief BLOCK: Upon arrival of an mined block
 */
void user_request_event_handler(hash_node_t *node, int forwarder, Msg *msg) {
    
    #ifdef TRACE_DISSEMINATION
    float difference;
    difference = simclock - msg->block.block_static.timestamp;
    fprintf(fp_print_trace, "R %010u %010u %03u\n", node->data->key, msg->block.block_static.transid, (int)difference);
    #endif

    // Calling the appropriate LUNES user level handler
    lunes_user_request_event_handler(node, forwarder, msg);
}


/****************************************************************************
 *! \brief LINK: upon arrival of a link request some tasks have to be executed
 */
void user_link_event_handler(hash_node_t *node, int id) {
    value_element val;

    val.value = id;

    // Adding a new entry in the local state of the registering node
    //	first entry	= key
    //	second entry	= value
    //	note: no duplicates are allowed
    if (add_entity_state_entry(id, &val, node->data->key, node) == -1) {
        // Insertion aborted, the key is already in the hash table
        /*fprintf(stdout, "%12.2f node: FATAL ERROR, [%5d] key %d (value %d) is a duplicate and can not be inserted in the hash table of local state\n", simclock, node->data->key, id, id);
        fflush(stdout);
        exit(-1);*/
    } else {
   		node->data->num_neighbors++;
   	}

    #ifdef AG_DEBUG
    fprintf(stdout, "%12.2f node: [%5d] received a link request from agent [%5d], total received requests: %d\n", simclock, node->data->key, id, g_hash_table_size(node->data->state));
    #endif
}


void user_unlink_event_handler(hash_node_t *node, int id) {
    delete_entity_state_entry(id, node);
    if (node->data->num_neighbors > 0){
    	node->data->num_neighbors --;
	}
}


/*****************************************************************************
 *! \brief REGISTER: a new SE (in this LP) has been created, now it is possibile to
 *         initialize its data structures (es. local state)
 */
void user_register_event_handler(hash_node_t *node, int id) {
    // Initializing the local data structures of the node
    node->data->state = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);
    // Calling the appropriate LUNES user level handler
}

/*****************************************************************************
 *      NOTIFY MIGRATION: a local SE will be migrated in another LP.
 *      This notification is reported to the user level but usually nothing
 *      has to be done
 */
void user_notify_migration_event_handler() {
    // Nothing to do
}

/*****************************************************************************
 *! \brief NOTIFY EXTERNAL MIGRATION: SEs that are allocated in other LPs are going
 *      to be migrated, this LP is notified of this update but the user level
 *      usually does not care of it
 */
void  user_notify_ext_migration_event_handler() {
    // Nothing to do
}

/*****************************************************************************
 *! \brief MIGRATION: migration-event manager (the real migration handler)
 *         A new migration message for this LP has been received, the trasported SE has
 *         been created and inserted in the data structures. Now it is necessary to
 *         perform some user level tasks such as taking care of de-serializing the
 *         SE's local state
 */
void user_migration_event_handler(hash_node_t *node, int id, Msg *msg) {
    // Initializing the local data structures of the node
    node->data->state = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, NULL);

    // The migration message contains the state of the migrating SE,
    //	after allocating space to locally manage the node, I've
    //	to update now the state of the SE using the state
    //	information contained in the migration message
}

/*****************************************************************************
 *! \brief CONTROL: at each timestep, the LP calls this handler to permit the execution
 *      of model level interactions, for performance reasons the handler is called once
 *      for all the SE that allocated in the LP
 */
void user_control_handler() {
    int          h;
    hash_node_t *node;
    hash_node_t *tempNode;

    if (simclock == ((float)BUILDING_STEP) + 1) {         //just once: build network topology ignoring non-active nodes       
        // Loading the graph topology that was previously generated
        lunes_load_graph_topology();
    }

    if ((int)simclock > EXECUTION_STEP && (int)simclock % env_max_ttl == 0 && simclock < env_end_clock - env_max_ttl){   //start of an epoch: chosing each time a new aplicant and holder
        //chosing applicant node
        int rnd; 
        do {
        	rnd = RND_Interval(S, 0, NLP * NSIMULATE);
            tempNode = hash_lookup(table, rnd);
        } while ( tempNode->data->status == 0);
        applicant = tempNode->data->key;
        //choosing holder node
        do {
        	rnd = RND_Interval(S, 0, NLP * NSIMULATE);
            tempNode = hash_lookup(table, rnd);
        } while ( tempNode->data->status == 0 || tempNode->data->key == applicant);
    	holder = tempNode->data->key;
    }

    // Only if in the aggregation phase is finished &&
    // if it is possible to send messages up to the last simulated timestep then the statistics will be
    // affected by some messages that have been sent but with no time to be received
    if ((simclock >= (float)BUILDING_STEP) && (simclock < (env_end_clock - MAX_TTL))) {
        // For each local SE
        for (h = 0; h < stable->size; h++) {
            for (node = stable->bucket[h]; node; node = node->next) {
                // Calling the appropriate LUNES user level handler
                lunes_user_control_handler(node);
            } 
        }
    }
}

/*****************************************************************************
 *! \brief USER MODEL: when it is received a model level interaction, after some
 *         validation this generic handler is called. The specific user level
 *         handler will complete its processing
 */
void user_model_events_handler(int to, int from, Msg *msg, hash_node_t *node) {

    // A model event has been received, now calling appropriate user level handler

    // If the node should perform a DOS attack: not a miner and is an attacker
    switch (msg->type) {
    // A transaction message
    case 'R':
        user_request_event_handler(node, from, msg);
        break;

    // A link message
    case 'L':
        user_link_event_handler(node, from);
        break;
     // A link message
    case 'U':
        user_unlink_event_handler(node, from);
        break;

    default:
        fprintf(stdout, "FATAL ERROR, received an unknown user model event type: %d\n", msg->type);
        fflush(stdout);
        exit(-1);
    }
}

void user_environment_handler() {
    // ######################## RUNTIME CONFIGURATION SECTION ####################################
    //	Runtime configuration:	migration type configuration
    env_migration = atoi(check_and_getenv("MIGRATION"));
    fprintf(stdout, "LUNES____[%10d]: MIGRATION, migration variable set to %d\n", local_pid, env_migration);
    if ((env_migration > 0) && (env_migration < 4)) {
        fprintf(stdout, "LUNES____[%10d]: MIGRATION is ON, migration type is set to %d\n", local_pid, env_migration);
        GAIA_SetMigration(env_migration);
    }else {
        fprintf(stdout, "LUNES____[%10d]: MIGRATION is OFF\n", local_pid);
        GAIA_SetMigration(MIGR_OFF);
    }

    //	Runtime configuration:	migration factor (GAIA)
    env_migration_factor = atof(check_and_getenv("MFACTOR"));
    fprintf(stdout, "LUNES____[%10d]: MFACTOR, migration factor: %f\n", local_pid, env_migration_factor);
    GAIA_SetMF(env_migration_factor);

    //	Runtime configuration:	turning on/off the load balancing (GAIA)
    env_load = atoi(check_and_getenv("LOAD"));
    fprintf(stdout, "LUNES____[%10d]: LOAD, load balancing: %d\n", local_pid, env_load);
    if (env_load == 1) {
        fprintf(stdout, "LUNES____[%10d]: LOAD, load balancing is ON\n", local_pid);
        GAIA_SetLoadBalancing(LOAD_ON);
    }else {
        fprintf(stdout, "LUNES____[%10d]: LOAD, load balancing is OFF\n", local_pid);
        GAIA_SetLoadBalancing(LOAD_OFF);
    }

    //	Runtime configuration:	number of steps in the simulation run
    env_end_clock = atof(check_and_getenv("END_CLOCK"));
    fprintf(stdout, "LUNES____[%10d]: END_CLOCK, number of steps in the simulation run -> %f\n", local_pid, env_end_clock);
    if (env_end_clock == 0) {
        fprintf(stdout, "LUNES____[%10d]:  END_CLOCK is 0, no timesteps are defined for this run!!!\n", local_pid);
    }

    env_perc_active_nodes_ = atof(check_and_getenv("ACTIVE_PERC"));
    fprintf(stdout, "LUNES____[%10d]: ACTIVE_PERC, initial percentage of active nodes -> %d\n", local_pid, env_perc_active_nodes_);
    if (env_perc_active_nodes_ <= 0) {
        fprintf(stdout, "LUNES____[%10d]: ACTIVE_PERC is <= 0, error \n", local_pid);
    }

    //	Runtime configuration:	time-to-live for new messages in the network
    env_max_ttl = atoi(check_and_getenv("MAX_TTL"));
    fprintf(stdout, "LUNES____[%10d]: MAX_TTL, maximum time-to-live for messages in the network -> %d\n", local_pid, env_max_ttl);

    if (env_max_ttl == 0) {
        fprintf(stdout, "LUNES____[%10d]: MAX_TTL is 0, no TTL is defined for this run!\n", local_pid);
    }


    //	Runtime configuration:	dissemination mode (gossip protocol)
    env_dissemination_mode = atoi(check_and_getenv("DISSEMINATION"));
    fprintf(stdout, "LUNES____[%10d]: DISSEMINATION, dissemination mode -> %d\n", local_pid, env_dissemination_mode);
    //
    switch (env_dissemination_mode) {
    case BROADCAST:                             //	probabilistic broadcast dissemination

        //	Runtime configuration:	probability threshold of the broadcast dissemination
        env_broadcast_prob_threshold = atof(check_and_getenv("BROADCAST_PROB_THRESHOLD"));
        fprintf(stdout, "LUNES____[%10d]: BROADCAST_PROB_THRESHOLD, probability of the broadcast dissemination -> %f\n", local_pid, env_broadcast_prob_threshold);
        if ((env_broadcast_prob_threshold < 0) || (env_broadcast_prob_threshold > 100)) {
            fprintf(stdout, "LUNES____[%10d]: BROADCAST_PROB_THRESHOLD is out of the boundaries!!!\n", local_pid);
        }
        break;

    case GOSSIP_FIXED_PROB:                     //	gossip with fixed probability

        //	Runtime configuration:	probability threshold of the fixed probability dissemination
        env_fixed_prob_threshold = atof(check_and_getenv("FIXED_PROB_THRESHOLD"));
        fprintf(stdout, "LUNES____[%10d]: FIXED_PROB_THRESHOLD, probability of the fixed probability dissemination -> %f\n", local_pid, env_fixed_prob_threshold);
        if ((env_fixed_prob_threshold < 0) || (env_fixed_prob_threshold > 100)) {
            fprintf(stdout, "LUNES____[%10d]:  FIXED_PROB_THRESHOLD is out of the boundaries!!!\n", local_pid);
        }
        break;
        
        //  Runtime configuration:  setting number of stem and stem phase for dandelion
    case DANDELION:
    case DANDELIONPLUS:
    case DANDELIONPLUSPLUS:
        env_dandelion_stem_steps = atof(check_and_getenv("DANDELION_STEPS_STEM_PHASE"));
        fprintf(stdout, "LUNES____[%10d]: DANDELION_STEPS_STEM_PHASE -> %f\n", local_pid, env_dandelion_stem_steps);
        if ( ( env_dandelion_stem_steps < 0 ) || ( env_dandelion_stem_steps > env_max_ttl ) ) {
            fprintf(stdout, "LUNES____[%10d]:  DANDELION_STEPS_STEM_PHASE is out of the boundaries!!!\n", local_pid);
        }   

    break;

    case DEGREE_DEPENDENT_GOSSIP:

        // Runtime configuration: probability function to be applied to Degree Dependent Gossip
        env_probability_function = atoi(check_and_getenv("PROBABILITY_FUNCTION"));
        fprintf(stdout, "LUNES____[%10d]: PROBABILITY_FUNCTION, probability function -> %u\n", local_pid, env_probability_function);

        // Coefficient of probability function
        env_function_coefficient = atof(check_and_getenv("FUNCTION_COEFFICIENT"));
        fprintf(stdout, "LUNES____[%10d]: FUNCTION_COEFFICIENT, function coefficient -> %f\n", local_pid, env_function_coefficient);

        break;


    case FIXED_FANOUT:
    break;

    default:
        fprintf(stdout, "LUNES____[%10d]: FATAL ERROR, the dissemination mode [%2d] is NOT implemented in this version of LUNES!!!\n", local_pid, env_dissemination_mode);
        fflush(stdout);
        exit(-1);
        break;
    }

}

/*****************************************************************************
 *! \brief BOOTSTRAP: before starting the real simulation tasks, the model level
 *         can initialize some data structures and set parameters
 */
void user_bootstrap_handler() {
    #ifdef TRACE_DISSEMINATION
    char buffer[1024];

    // Preparing the simulation trace file
    sprintf(buffer, "%sSIM_TRACE_%03d.log", TESTNAME, LPID);

    fp_print_trace = fopen(buffer, "w");
    #endif
}

/*****************************************************************************
 *! \brief SHUTDOWN: Before shutting down, the model layer is able to
 *         deallocate some data structures
 */
void user_shutdown_handler() {
    #ifdef TRACE_DISSEMINATION
    char  buffer[1024];
    FILE *fp_print_messages_trace;


    sprintf(buffer, "%stracefile-messages-%d.trace", TESTNAME, LPID);

    fp_print_messages_trace = fopen(buffer, "w");

    //	statistics
    //	total number of trans on the network
    fprintf(fp_print_messages_trace, "M %010lu\n", get_total_sent_trans());
    //	total number of block broadcasted on the network
    fprintf(fp_print_messages_trace, "M %010lu\n", get_total_sent_blocks());

    fclose(fp_print_messages_trace);

    fclose(fp_print_trace);
    #endif
}