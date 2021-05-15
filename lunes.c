/*	##############################################################################################
 *      Advanced RTI System, ARTÌS			http://pads.cs.unibo.it
 *      Large Unstructured NEtwork Simulator (LUNES)
 *
 *      Description:
 *              For a general introduction to LUNES implmentation please see the
 *              file: mig-agents.c
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
#include <values.h>
#include "utils.h"
#include "user_event_handlers.h"
#include "lunes.h"
#include "lunes_constants.h"
#include "entity_definition.h"


/* ************************************************************************ */
/*       L O C A L	V A R I A B L E S			                            */
/* ************************************************************************ */

FILE *         fp_print_trace;        // File descriptor for simulation trace file
unsigned short env_max_ttl = MAX_TTL; // TTL of newly created messages


/* ************************************************************************ */
/*          E X T E R N A L     V A R I A B L E S                           */
/* ************************************************************************ */

extern hash_t hash_table, *table;                   /* Global hash table of simulated entities */
extern hash_t sim_table, *stable;                   /* Hash table of locally simulated entities */
extern double simclock;                             /* Time management, simulated time */
extern TSeed  Seed, *S;                             /* Seed used for the random generator */
extern char * TESTNAME;                             /* Test name */
extern int    NSIMULATE;                            /* Number of Interacting Agents (Simulated Entities) per LP */
extern int    NLP;                                  /* Number of Logical Processes */
// Simulation control
extern unsigned short env_dissemination_mode;       /* Dissemination mode */
extern float          env_broadcast_prob_threshold; /* Dissemination: conditional broadcast, probability threshold */
extern float          env_fixed_prob_threshold;     /* Dissemination: fixed probability, probability threshold */
extern float   		  env_dandelion_stem_steps;	    /* Dissemination: dandelion, number of stem steps */
extern unsigned int   env_probability_function;     /* Probability function for Degree Dependent Gossip */
extern double         env_function_coefficient;     /* Coefficient of the probability function */
extern int            applicant;                    /* ID of the applicant node*/
extern int            holder;                       /* ID of the holder node*/
extern unsigned short env_max_ttl;                  /* TTL of new messages */
extern int 			  env_perc_active_nodes_;		/* Initial percentage of active node*/
extern long 		  countMessages;
extern int 			  countEpochs;
extern int 			  countDelivers;
extern double		  countSteps;


int tempcountLinks=0;
int tempcountActive=0;


/*! \brief Used to calculate the forwarding probability value for a given node
 */
double lunes_degdependent_prob(unsigned int deg) {
    double prob = 0.0;

    switch (env_probability_function) {
    // Function 2
    case 2:
        prob = 1.0 / log(env_function_coefficient * deg);
        break;

    // Function 1
    case 1:
        prob = 1.0 / pow(deg, env_function_coefficient);
        break;

    default:
        fprintf(stdout, "%12.2f FATAL ERROR, function: %d does NOT exist!\n", simclock, env_probability_function);
        fflush(stdout);
        exit(-1);
        break;
    }

    /*
     * The probability is defined in the range [0, 1], to get full dissemination some functions require that the negative
     * results are treated as "true" (i.e. 1)
     */
    if ((prob < 0) || (prob > 1)) {
        prob = 1;
    }
    return(prob);
}

int is_in_array (gpointer arr[], int length, gpointer elem){
	int i;
	for (i=0; i< length; i++){
		if (arr[i] == NULL){
			return 0;
		}
		else if (arr[i] == elem){
			return 1;
		}
	}
	return 0;
}

int is_in_stem_mode (hash_node_t *node){
    int epoch = (int)simclock / env_max_ttl;
	if ((node->data->key + epoch * 7) % 100 <= env_dandelion_stem_steps){  //is in stem mode, dependent on key and epoch
		return 1;
	}
	return 0;
}

/*! \brief Used to forward a received message to all (or some of)
 *         the neighbors of a given node. BlockMsg have max priority
 *         so all node will broadcast this messages.
 *         Transactions are regulated by the dissemination protocol
 *         implementation.
 *
 *  @param[in] node: The node doing the forwarding
 *  @param[in] msg: Message to forward
 *  @param[in] ttl: TTL of the message
 *  @param[in] timestamp: Timestamp of message's arrival
 *  @param[in] creator: Node sender
 *  @param[in] forwarder: Agent forwarder
 */
void lunes_real_forward(hash_node_t *node, Msg *msg, unsigned short ttl, float timestamp, int id, unsigned int creator, unsigned int forwarder) {
    // Iterator to scan the whole state hashtable of neighbors
    GHashTableIter iter;
    gpointer       key, destination;
    float          threshold;         // Tmp, used for probabilistic-based dissemination algorithms
    hash_node_t *  sender, *receiver; // Sender and receiver nodes in the global hashtable

        // Dissemination mode for the forwarded messages (dissemination algorithm)
    switch (env_dissemination_mode) {
        case BROADCAST:
            // The message is forwarded to ALL neighbors of this node
            // NOTE: in case of probabilistic broadcast dissemination this function is called
            //		 only if the probabilities evaluation was positive
            g_hash_table_iter_init(&iter, node->data->state);

            // All neighbors
            while (g_hash_table_iter_next(&iter, &key, &destination)) {
                sender   = hash_lookup(stable, node->data->key);             // This node
                receiver = hash_lookup(table, *(unsigned int *)destination); // The neighbor

                // The original forwarder of this message and its creator are exclueded
                // from this dissemination
                if ((receiver->data->key != forwarder) && (receiver->data->key != creator)) {
                    execute_request (simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
                }
            }
            break;

        case DANDELION:
        case DANDELIONPLUS:
            g_hash_table_iter_init (&iter, node->data->state);
            if (env_max_ttl - ttl <=  env_dandelion_stem_steps ){                   //stem phase
            	if (node->data->num_neighbors > 0){
	    	        sender   = hash_lookup(stable, node->data->key);             // This node
	                destination = hash_table_random_key(node->data->state);                    
	                receiver = hash_lookup(table, *(unsigned int *)destination);        // The neighbor
	                execute_request (simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
	            } 
            } else {                                                                //fluff phase, sending messages to everyone, except the forwarder
                while (g_hash_table_iter_next (&iter, &key, &destination)) {

                    sender = hash_lookup(stable, node->data->key);                  // This node
                    receiver = hash_lookup(table, *(unsigned int *)destination);    // The neighbor

                    if (receiver->data->key != forwarder )
                        execute_request(simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
                }
            }
        break;

        case DANDELIONPLUSPLUS:
            g_hash_table_iter_init (&iter, node->data->state);
           	if ( is_in_stem_mode(node) ==0) {           

	            while (g_hash_table_iter_next (&iter, &key, &destination)) {
	                sender = hash_lookup(stable, node->data->key);                  // This node
	                receiver = hash_lookup(table, *(unsigned int *)destination);    // The neighbor

	                if (receiver->data->key != forwarder )
	                    execute_request(simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
            	} 
            } else {

            	if (node->data->num_neighbors > 0){
	    	        sender   = hash_lookup(stable, node->data->key);             // This node
	                destination = hash_table_random_key(node->data->state);                    
	                receiver = hash_lookup(table, *(unsigned int *)destination);        // The neighbor
	                execute_request (simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
	            } 
            }

        break;

        case GOSSIP_FIXED_PROB:
            // In this case, all neighbors will be analyzed but the message will be
            // forwarded only to some of them
            g_hash_table_iter_init(&iter, node->data->state);

            // All neighbors
            while (g_hash_table_iter_next(&iter, &key, &destination)) {
                // Probabilistic evaluation
                threshold = RND_Interval(S, (double)0, (double)100);

                if (threshold <= env_fixed_prob_threshold) {
                    sender   = hash_lookup(stable, node->data->key);             // This node
                    receiver = hash_lookup(table, *(unsigned int *)destination); // The neighbor

                    // The original forwarder of this message and its creator are exclueded
                    // from this dissemination
                    if ((receiver->data->key != forwarder) && (receiver->data->key != creator)) {
                        execute_request(simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
                    }
                }
            }
            break;
        // Degree Dependent dissemination algorithm
        case DEGREE_DEPENDENT_GOSSIP:
            g_hash_table_iter_init(&iter, node->data->state);

            // All neighbors
            while (g_hash_table_iter_next(&iter, &key, &destination)) {
                sender   = hash_lookup(stable, node->data->key);             // This node
                receiver = hash_lookup(table, *(unsigned int *)destination); // The neighbor

                // The original forwarder of this message and its creator are excluded
                // from this dissemination
                if ((receiver->data->key != forwarder) && (receiver->data->key != creator)) {
                    // Probabilistic evaluation
                    threshold = (RND_Interval(S, (double)0, (double)100)) / 100;

                    // If the eligible recipient has less than 3 neighbors, its reception probability is 1. However,
                    // if its value of num_neighbors is 0, it means that I don't know the dimension of
                    // that node's neighborhood, so the threshold is set to 1/n, being n
                    // the dimension of my neighborhood
                    if (receiver->data->num_neighbors < 3) {
                        // Note that, the startup phase (when the number of neighbors is not known) falls in
                        // this case (num_neighbors = 0)
                        // -> full dissemination
                        execute_request(simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
                    }
                    // Otherwise, the probability is evaluated according to the function defined by the
                    // environment variable env_probability_function
                    else{
                        if (threshold <= lunes_degdependent_prob(receiver->data->num_neighbors)) {
                            execute_request(simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
                        }
                    }
                }
            }
            break;

            case FIXED_FANOUT:
        	g_hash_table_iter_init (&iter, node->data->state);
        	sender   = hash_lookup(stable, node->data->key);             // This node

        	if (node->data->num_neighbors <= 3) {
        		while (g_hash_table_iter_next(&iter, &key, &destination)) {
	                receiver = hash_lookup(table, *(unsigned int *)destination); // The neighbor

	                // The original forwarder of this message and its creator are exclueded
	                // from this dissemination
	                if ((receiver->data->key != forwarder) && (receiver->data->key != creator)) {
	                    execute_request (simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
	                }
	            }
        	} else {
        		int count = 0;

        		threshold = RND_Interval(S, (double)0, (double)100);
        		int number = 3;
        		gpointer arr [number];
        		while (count < number){                  
        			destination = hash_table_random_key(node->data->state); 
                	receiver = hash_lookup(table, *(unsigned int *)destination);        // The neighbor     
        			if (is_in_array(arr, number, destination)==0){
        				arr [count] = destination;
      				 	count++;
        				if ((receiver->data->key != forwarder) && (receiver->data->key != creator)) {
		                	execute_request (simclock + FLIGHT_TIME, sender, receiver, ttl, id, timestamp, creator);
		            	}
        			}
        		}
        	}

        	break;
    }
}

/*! \brief Dissemination protocol implementation.
 *         A new message has been received from a neighbor,
 *         it is necessary to forward it in some way
 *
 *  @param[in] node: The node doing the forwarding
 *  @param[in] msg: Message to forward
 *  @param[in] ttl: TTL of the message
 *  @param[in] timestamp: Timestamp of message's arrival
 *  @param[in] creator: Node sender
 *  @param[in] forwarder: Agent forwarder
 */
void lunes_forward_to_neighbors(hash_node_t *node, Msg *msg, unsigned short ttl, float timestamp, int id, unsigned int creator, unsigned int forwarder) {
    float threshold; // Tmp, probabilistic evaluation

    // Dissemination mode for the forwarded messages
    switch (env_dissemination_mode) {
    case BROADCAST:
        // Probabilistic evaluation
        threshold = RND_Interval(S, (double)0, (double)100);
        if (threshold <= env_broadcast_prob_threshold) {
            lunes_real_forward(node, msg, ttl, timestamp, id, creator, forwarder);
        }
        break;

    case GOSSIP_FIXED_PROB:      
    case DANDELION:
    case DANDELIONPLUS:
    case DANDELIONPLUSPLUS:
    case DEGREE_DEPENDENT_GOSSIP:
    case FIXED_FANOUT:
        lunes_real_forward(node, msg, ttl, timestamp, id, creator, forwarder);
        break;

    default:
        fprintf(stdout, "%12.2f FATAL ERROR, the dissemination mode [%2d] is NOT implemented in this version of LUNES!!!\n", simclock, env_dissemination_mode);
        fprintf(stdout, "%12.2f NOTE: all the adaptive protocols require compile time support: see the ADAPTIVE_GOSSIP_SUPPORT define in sim-parameters.h\n", simclock);
        fflush(stdout);
        exit(-1);
        break;
    }
}


void lunes_send_request_to_neighbors(hash_node_t *node, int req_id) {
    // Iterator to scan the whole state hashtable of neighbors
    GHashTableIter iter;
    gpointer       key, destination;

    // All neighbors
    g_hash_table_iter_init(&iter, node->data->state);

    while (g_hash_table_iter_next(&iter, &key, &destination)) {
        execute_request(simclock + FLIGHT_TIME, hash_lookup(stable, node->data->key), hash_lookup(table, *(unsigned int *)destination), env_max_ttl, req_id, simclock, node->data->key);
    }
}



/* -----------------------   GRAPHVIZ DOT FILES SUPPORT --------------------- */

/*! \brief Support function for the parsing of graphviz dot files,
 *         used for loading the graphs (i.e. network topology)
 */
void lunes_dot_tokenizer(char *buffer, int *source, int *destination) {
    char *token;
    int   i = 0;

    token = strtok(buffer, "--");
    do {
        i++;

        if (i == 1) {
            *source = atoi(token);
        }

        if (i == 2) {
            token[strlen(token) - 1] = '\0';
            *destination             = atoi(token);
        }
    } while ((token = strtok(NULL, "--")));
}

/*! \brief Parsing of graphviz dot files,
 *         used for loading the graphs (i.e. network topology)
 */
void lunes_load_graph_topology() {
    FILE *dot_file;
    char  buffer[1024];
    int   source      = 0,
          destination = 0;
    hash_node_t *source_node,
                *destination_node;
    value_element val;
    // What's the file to read?
    sprintf(buffer, "%s%s", TESTNAME, TOPOLOGY_GRAPH_FILE);
    dot_file = fopen(buffer, "r");

    // Reading all of it
    while (fgets(buffer, 1024, dot_file) != NULL) {
        // Parsing line by line
        lunes_dot_tokenizer(buffer, &source, &destination);

        // I check all the edges defined in the dot file to build up "link messages"
        // between simulated entities in the simulated network model

        // Is the source node a valid simulated entity?
        if ((source_node = hash_lookup(stable, source)))  {
            // Is destination vertex a valid simulated entity?
            if ((destination_node = hash_lookup(table, destination))) {

            	if (destination_node->data->status != 0 && source_node->data->status != 0){
	                #ifdef AG_DEBUG
	                fprintf(stdout, "%12.2f node: [%5d] adding link to [%5d]\n", simclock, source_node->data->key, destination_node->data->key);
	                #endif

	                // Creating a link between simulated entities (i.e. sending a "link message" between them)
	                execute_link(simclock + FLIGHT_TIME, source_node, destination_node);

	                // Initializing the extra data for the new neighbor
	                val.value = destination;

	                // I've to insert the new link (and its extra data) in the neighbor table of this sender,
	                // the receiver will do the same when receiving the "link request" message

	                // Adding a new entry in the local state of the sender
	                //	first entry	= key
	                //	second entry	= value
	                //	note: no duplicates are allowed
	                if (add_entity_state_entry(destination, &val, source, source_node) == -1) {
	                    // Insertion aborted, the key is already in the hash table
	                    fprintf(stdout, "%12.2f node: FATAL ERROR, [%5d] key %d (value %d) is a duplicate and can not be inserted in the hash table of local state\n", simclock, source, destination, destination);
	                    fflush(stdout);
	                    exit(-1);
	                }
	            }

            }else {
                fprintf(stdout, "%12.2f FATAL ERROR, destination: %d does NOT exist!\n", simclock, destination);
                fflush(stdout);
                exit(-1);
            }
        }
    }

    fclose(dot_file);
}


int percentage_to_deactivate(int activate_perc){    //denominator = 10000
	int ratio = (int) env_perc_active_nodes_ / (100 - env_perc_active_nodes_);
	#ifndef HIERARCHY
	return (int) activate_perc / ratio;
	#else
	return (int) activate_perc / ratio - 1;
	#endif
}

int count_neighbors (hash_node_t *node){
	GHashTableIter iter;
    gpointer       key, destination;
	int count =0;
	g_hash_table_iter_init(&iter, node->data->state);
    while (g_hash_table_iter_next(&iter, &key, &destination)) {
    	count++;
    }	
    return count;
}

void print_neighbors (hash_node_t *node){
	GHashTableIter iter;
    gpointer       key, destination;
    hash_node_t *  neigh;
	g_hash_table_iter_init(&iter, node->data->state);
    while (g_hash_table_iter_next(&iter, &key, &destination)) {
    	neigh = hash_lookup(table, *(unsigned int *)destination);
    	fprintf(stdout, "%d,%d,0\n", node->data->key, neigh->data->key);
    }	
}


void attach_node (hash_node_t *node){
	int connections = RND_Interval(S, 5,11); //how many connections to establish  #12-19 to get 12 edges per node, #5-11 to get 6 edges per node, #8-15 to get 9 edges per node
	int count = 0;
	while (count < connections){
		int new_neighbor_id = RND_Interval(S, 0, (NLP*NSIMULATE)); // chose a random node of the graph
		hash_node_t * new_neighbor = hash_lookup(table, new_neighbor_id); // The neighbor
		if (new_neighbor_id != node->data->key && new_neighbor->data->status != 0){		
			value_element val;
		    val.value = new_neighbor_id;	
		    #ifdef HIERARCHY
			int prob = RND_Interval(S, 0, (400)); // chose a random node of the graph
			if((prob < 160 && simclock < 400) || prob < 80){
				new_neighbor_id = prob % 80;
				new_neighbor = hash_lookup(table, new_neighbor_id);
				val.value = new_neighbor_id;	
			}
		    #endif
		    if (add_entity_state_entry(new_neighbor_id, &val, node->data->key, node) != -1) {
		    	execute_link(simclock + FLIGHT_TIME, node, new_neighbor);
		    	count++;
            }
		}
	}
	node->data->num_neighbors = connections;
}  


void detach_node (hash_node_t *node){
	GHashTableIter iter;
    gpointer       key, destination;
    hash_node_t *  toDel;

	g_hash_table_iter_init(&iter, node->data->state);
    while (g_hash_table_iter_next(&iter, &key, &destination)) {
    	toDel = hash_lookup(table, *(unsigned int *)destination);   // The neighbor
    	execute_unlink(simclock + FLIGHT_TIME, node, toDel);		// To signal that node has deactivated so the link is broken
    	execute_unlink(simclock + FLIGHT_TIME, toDel, node) ;       //link will be actually removed at the next step
    }	
    node->data->num_neighbors = 0;
} 



/****************************************************************************
 *! \brief LUNES_CONTROL: node activity for the current timestep
 * @param[in] node: Node that execute actions
 */
void lunes_user_control_handler(hash_node_t *node) {
	#ifdef HIERARCHY
	if (node->data->key >80){
	#endif

	if (simclock == BUILDING_STEP){						//just once: Building graph topology
		int rnd = RND_Interval(S, 0, 100);
		if (rnd >= env_perc_active_nodes_){
			node->data->status = 0;
		}
	}	
			
	#ifdef HIERARCHY
	}
	#endif
	/*
	if ((int)simclock % 10000 == 0 && node->data->status != 0 && node->data->key < 80){
		fprintf(stdout, "at %f node %d has %d\n", simclock, node->data->key, node->data->num_neighbors );
	}*/
	

    if (simclock == env_max_ttl){						//just once: counting neighbors
		GHashTableIter iter;
	    gpointer       key, destination;
	    g_hash_table_iter_init(&iter, node->data->state);
	    int count = 0;
	        // All neighbors
	    while (g_hash_table_iter_next(&iter, &key, &destination)) {
	        count = count +1;
	    }
	    node->data->num_neighbors = count;
    }

    if (node->data->status != 0 && node->data->num_neighbors == 0 && simclock > env_max_ttl){
    	attach_node (node);
    }


    
    if ((int) simclock % env_max_ttl == 0 && (int)simclock >= env_max_ttl) {  		// at the beginnning of each epoch
    	//if (node->data->status > 0){
    	tempcountLinks = tempcountLinks + node->data->num_neighbors;   				// temp to delete
    	tempcountActive = (node->data->status != 0) ? tempcountActive + 1 : tempcountActive;
        //}
    	node->data->received = 0;

    	if (node->data->status != 0){
    		node->data->status = 1;
    	}
    	if (node->data->key == applicant){
    		node->data->status = 2;
    	}
    	if (node->data->key == holder){
    		node->data->status = 3;
    	}

    	if (node->data->key == applicant && simclock > 400){    // > 400 because one waits the network to stabilize
    		countEpochs++;
    		if (env_dissemination_mode != DANDELIONPLUS && env_dissemination_mode != DANDELION &&  env_dissemination_mode != DANDELIONPLUSPLUS){
    			lunes_send_request_to_neighbors(node, 0);
    			node->data->received = (int)simclock;
    		} else{
    			RequestMsg     msg;
                // Defining the message type
                msg.request_static.type = 'R';
                msg.request_static.timestamp = simclock;
                msg.request_static.ttl       = env_max_ttl;
                msg.request_static.creator   = node->data->key;
                Msg m = (Msg) msg;
    			lunes_forward_to_neighbors(node, &m, --(msg.request_static.ttl), simclock, 0, msg.request_static.creator, node->data->key);                            
    			node->data->received = (int)simclock;				//for Dandelion++
    		}
    	}
	}

	if ((int)simclock > env_max_ttl) {							// at each step there is the chance for a node to activate or deactivate 
		int rnd = RND_Interval(S, 0, 10000);
		if (node->data->status == 0){               
			if (rnd < 100){                        				//1% for an off node to activate
				node->data->status = 1;
				attach_node(node);
			}
		}
		else if (node->data->status == 1 || node->data->status == 5){    
		#ifdef HIERARCHY 
		if (node->data->key>=80){
		#endif     
			if (rnd < percentage_to_deactivate (100)){
				node->data->status = 0;
				detach_node(node);
			}
		#ifdef HIERARCHY
		}
		#endif
		}
	}


	// dandelion++ recovery mechanism: nodes that received the message in the stem phase start the fluff phase if they don't receive the message back in
	if ((env_dissemination_mode == DANDELIONPLUS  && node->data->received > 0 && simclock > 400 && node->data->status !=0 &&                      //DANDELIONPLUS
	   simclock - node->data->received > env_dandelion_stem_steps + 4 && node->data->received % env_max_ttl <= env_dandelion_stem_steps) ||
		(env_dissemination_mode == DANDELIONPLUSPLUS  && node->data->received > 0 && simclock > 400 && node->data->status !=0 &&                      //DANDELION++
	   simclock - node->data->received > 7 && node->data->received % env_max_ttl <= env_dandelion_stem_steps && is_in_stem_mode(node)==1 )){         
		RequestMsg     msg;
        msg.request_static.type = 'R';
        msg.request_static.timestamp = simclock;
        msg.request_static.ttl       = env_max_ttl - ((int)simclock % env_max_ttl);
        msg.request_static.creator   = node->data->key;
        Msg m = (Msg) msg;
		lunes_forward_to_neighbors(node, &m, --(msg.request_static.ttl), simclock, 0, msg.request_static.creator, node->data->key);                            
		
		node->data->received = -1;
	}

}

// request
void lunes_user_request_event_handler(hash_node_t *node, int forwarder, Msg *msg) {
	countMessages++;
	if (node->data->status == 3){  //if it's the holder node
		node->data->status = 4;
		countSteps += (int)simclock % env_max_ttl;
		countDelivers++;
	}
	else if (node->data->status == 1 
	|| (node->data->status != 0 && env_dissemination_mode == DANDELION && (int) simclock % env_max_ttl <= env_dandelion_stem_steps) //allows nodes int the stem phase to forward messages
	|| (node->data->status != 0 && env_dissemination_mode == DANDELIONPLUSPLUS && is_in_stem_mode(node)==1 ) 
	|| (node->data->status != 0 && env_dissemination_mode == DANDELIONPLUS && (int) simclock % env_max_ttl <= env_dandelion_stem_steps)){ 
		node->data->status = 5;
		lunes_forward_to_neighbors(node, msg,  --(msg->request.request_static.ttl),  msg->request.request_static.timestamp, (int)simclock, msg->request.request_static.creator, forwarder);
	}

	if (env_dissemination_mode==DANDELIONPLUS){
		if (node->data->received >= 0 && (int)simclock % env_max_ttl <= env_dandelion_stem_steps){
			node->data->received = (int) simclock;
		} else  {
			node->data->received = -1;
		}
	}

	if (env_dissemination_mode==DANDELIONPLUSPLUS && is_in_stem_mode(node)==1){
		if (node->data->received >= 0){
			node->data->received = (int) simclock;
		} else  {
			node->data->received = -1;
		}
	}
}