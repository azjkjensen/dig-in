#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>


#include "dns.h"

#define MAX_ENTRIES 1024
#define DNS_MSG_MAX 4096

#define LISTEN_QUEUE_SIZE 1024
#define BUFFER_MAX 1024

#define CNAME_TYPE 5

typedef struct
{
    dns_rr rr;
    time_t expires;
} dns_db_entry;

dns_db_entry cachedb[MAX_ENTRIES];
int cachedb_len;
time_t cachedb_start;
char *cachedb_file;

void init_db()
{
    /* 
	 * Import the cache database from the file specified by cachedb_file.
	 * Reset cachedb_start to the current time.  Zero the expiration
	 * for all unused cache entries to invalidate them.  Read each line in
	 * the file, create a dns_rr structure for each, set the expires time
	 * based on the current time and the TTL read from the file, and create
	 * an entry in the cachedb.  Zero the expiration for all unused cache
	 * entries to invalidate them.
	 *
	 * INPUT:  None
	 * OUTPUT: None
	 */
    printf("Initializing database!\n");
    char* ownerName = malloc(256);
    int ttl;
    char class[5];
    char type[5];
    char* rData = malloc(MAX_ENTRIES);

    FILE *fp;
    fp = fopen(cachedb_file, "r");
	int i = -1;
    while (fscanf(fp, "%s %d %s %s %s", ownerName, &ttl, class, type, rData) != EOF)
    {
		int class_d;
		if(strcmp(class, "IN") == 0){
			class_d = 1;
		} else{
			// Handle a wrong class?
		}

		int type_d;
		if(strcmp(type, "A") == 0){
			type_d = 1;
		} else if(strcmp(type, "CNAME") == 0){
			type_d = 5;
		}
		dns_rr rr;

		rr.name = ownerName;
		rr.ttl = ttl;
		rr.class = class_d;
		rr.type = type_d;
		rr.rdata = rData;
		rr.rdata_len = strlen(rData);

		// Wire-encode the rdata 
		char* d = malloc(512);
		if(rr.type == 1){
			inet_pton(AF_INET, rData, d);
			rr.rdata = d;
			rr.rdata_len = 4;
		}else if(rr.type == 5){
			printf("Non-ip rData\n");
			strcpy(d, rData);
			rr.rdata = d;
			rr.rdata_len = strlen(rData);
		}
		// printf("New data: ");
		// print_bytes(d, 4);

		printf("\nownerName %s\n", rr.name);
		printf("ttl %d\n", rr.ttl);
		printf("class %d\n", rr.class);
		printf("type %d\n", rr.type);
		printf("rData %s\n\n", rr.rdata);

		dns_db_entry entry;
		entry.rr = rr;
		entry.expires = time(NULL) + ttl;

		// free(rr);

		i++;
		cachedb[i] = entry;

		// We need to point to a new string to store it elsewhere on the heap.
		ownerName = malloc(256);
    }
	// Number of entries in the cache.
	cachedb_len = i + 1;
    printf("Finished reading DB. %d cache entries.\n", cachedb_len);
}

int is_valid_request(unsigned char *request)
{
    /* 
	 * Check that the request received is a valid query.
	 *
	 * INPUT:  request: a pointer to the array of bytes representing the
	 *                  request received by the server
	 * OUTPUT: a boolean value (1 or 0) which indicates whether the request
	 *                  is valid.  0 should be returned if the QR flag is
	 *                  set (1), if the opcode is non-zero (not a standard
	 *                  query), or if there are no questions in the query
	 *                  (question count != 1).
	 */
    int flags = ((request[2] << 8) | request[3]);
    int qr = (flags >> 15) & 0x01;
    int opcode = (flags >> 11) & 0x0f;
    int questionCount = (request[4] << 8) | request[5];
    //  printf("Flags: %#06x\n", flags);
    //  printf("qr: %d\n", qr);
    //  printf("opcode: %#04x\n", opcode);
    //  printf("question count: %d\n", questionCount);
    return (!qr && !opcode && (questionCount == 1));
}

dns_rr get_question(unsigned char *request)
{
    /* 
	 * Return a dns_rr for the question, including the query name and type.
	 *
	 * INPUT:  request: a pointer to the array of bytes representing the
	 *                  request received by the server.
	 * OUTPUT: a dns_rr structure for the question.
	 */

	 // Question starts at index 12
}

int get_response(unsigned char *request, int len, unsigned char *response)
{
    /* 
	 * Handle a request and produce the appropriate response.
	 *
	 * Start building the response:
	 *   copy ID from request to response;
	 *   clear all flags and codes;
	 *   set QR bit to 1;
	 *   copy RD flag from request;
	 *
	 * If the request is not valid:
	 *   set the response code to 1 (FORMERR).
	 *   set all section counts to 0
	 *   return 12 (length of the header only)
	 *
	 * Otherwise:
	 *
	 *   Continue building the response:
	 *     set question count to 1;
	 *     set authority and additional counts to 0
	 *     copy question section from request;
	 *
	 *   Search through the cache database for an entry with a matching
	 *   name and type which has not expired
	 *   (expiration - current time > 0).
	 *
	 *     If no match is found:
	 *       the answer count will be 0, the response code will
	 *       be 3 (NXDOMAIN or name does not exist).
	 *
	 *     Otherwise (a match is found):
	 *       the answer count will be 1 (positive), the response code will
	 *       be 0 (NOERROR), and the appropriate RR will be added to the
	 *       answer section.  The TTL for the RR in the answer section
	 *       should be updated to expiration - current time.
	 *
	 *   Return the length of the response message.
	 *
	 * INPUT:  request: a pointer to the array of bytes representing the
	 *                  request received by the server.
	 * INPUT:  len: the length (number of bytes) of the request
	 * INPUT:  response: a pointer to the array of bytes where the response
	 *                  message should be constructed.
	 * OUTPUT: the length of the response message.
	 */
    int isValidRequest = is_valid_request(request);
    printf("Valid request: %s\n", isValidRequest ? "True" : "False");


	// Get the query name and type
	int i = 12;
	dns_rr rr = rr_from_wire(request, &i, 1);
	printf("Request name: %s\n", rr.name);

	// Build the response based on the cache entry/lack thereof

	// First, copy the ID into the response.
	response[0] = request[0];
	response[1] = request[1];

	// Clear flags and codes except qr and rd to 0.
	// Set qr to 1 and rd to the value of rd from the request.
	response[2] = 0x80 | (request[2] & 0x01);
	response[3] = 0x00;

	int responseLength = 12;
	if(!isValidRequest){
		// Set the response code to 1 (FORMERR or “format error”)
		response[3] = 0x01;
		// Set the number of answer rr's to 0
		response[6] = 0x00;
		response[7] = 0x00;
		return responseLength;
	}

	// Set question count to 1.
	response[4] = 0x00;
	response[5] = 0x01;

	// Set authority and additional counts to 0.
	response[8] = 0x00;
	response[9] = 0x00;
	response[10] = 0x00;
	response[11] = 0x00;

	// Copy question section from request.
	int queryLength = rr_to_wire(rr, &(response[responseLength]), 1); 
	responseLength += queryLength;
	printf("Query Length: %d\n", queryLength);

	int matchIndex = -1;

	char* qname = malloc(512);
	strcpy(qname, rr.name);
	int rrCount = 0;
	// Look up the query in the cache
	for(int i = 0; i < cachedb_len; i++){
		dns_db_entry e = cachedb[i];
		// printf("Entry: %s\n", e.rr.name);
		if(strcmp(e.rr.name, qname) == 0 && e.rr.type == rr.type && time(NULL) < e.expires){
			// We have a match!
			printf("Found a match!\n");
			matchIndex = i;
			rrCount++;
			break;
		} else if(strcmp(e.rr.name, qname) == 0 && e.rr.type == CNAME_TYPE){
			printf("Found a matching CNAME!\n");
			// Add the matching rr to the response.
			qname = e.rr.rdata;
			// inet_ntop(AF_INET, e.rr.rdata, qname, 512);
			printf("New qname: %s\n", qname);

			rrCount++;
			qname = e.rr.rdata;
			// Return to the start of the loop.
			i = 0;
		}
	}

	if(matchIndex == -1){
		printf("No matching cache record in the database.\n");
		// Set the response code to 3 (NXDOMAIN)
		response[3] = 0x03;
		// Set the number of answer rr's to 0
		response[6] = 0x00;
		response[7] = 0x00;
		return 12;
	}

	// Set the response code to 0 (no error)
	response[3] = 0x00;
	// Set the number of answer rr's to rrCount.
	response[6] = 0x00;
	response[7] = rrCount;

	// Adjust the time-to-live on this cache entry.
	cachedb[matchIndex].rr.ttl = cachedb[matchIndex].expires - time(NULL);

	int answerRRLength = rr_to_wire(cachedb[matchIndex].rr, &(response[responseLength]), 0);
	responseLength += answerRRLength;

	printf("Current response: ");
	print_bytes(response, responseLength);
	return responseLength;
}

int create_udp_server_socket(char *port, int protocol)
{
    int sock;
    int ret;
    int optval = 1;
    struct addrinfo hints;
    struct addrinfo *addr_ptr;
    struct addrinfo *addr_list;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = protocol;
    /* AI_PASSIVE for filtering out addresses on which we
	 * can't use for servers
	 *
	 * AI_ADDRCONFIG to filter out address types the system
	 * does not support
	 *
	 * AI_NUMERICSERV to indicate port parameter is a number
	 * and not a string
	 *
	 * */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG | AI_NUMERICSERV;
    /*
	 *  On Linux binding to :: also binds to 0.0.0.0
	 *  Null is fine for TCP, but UDP needs both
	 *  See https://blog.powerdns.com/2012/10/08/on-binding-datagram-udp-sockets-to-the-any-addresses/
	 */
    ret = getaddrinfo(protocol == SOCK_DGRAM ? "::" : NULL, port, &hints, &addr_list);
    if (ret != 0){
	fprintf(stderr, "Failed in getaddrinfo: %s\n", gai_strerror(ret));
	exit(EXIT_FAILURE);
    }

    for (addr_ptr = addr_list; addr_ptr != NULL; addr_ptr = addr_ptr->ai_next){
		sock = socket(addr_ptr->ai_family, addr_ptr->ai_socktype, addr_ptr->ai_protocol);
		if (sock == -1){
			perror("socket");
			continue;
		}

		// Allow us to quickly reuse the address if we shut down (avoiding timeout)
		ret = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
		if (ret == -1){
			perror("setsockopt");
			close(sock);
			continue;
		}

		ret = bind(sock, addr_ptr->ai_addr, addr_ptr->ai_addrlen);
		if (ret == -1){
			perror("bind");
			close(sock);
			continue;
		}
		break;
    }
    freeaddrinfo(addr_list);
    if (addr_ptr == NULL){
		fprintf(stderr, "Failed to find a suitable address for binding\n");
		exit(EXIT_FAILURE);
    }

    if (protocol == SOCK_DGRAM){
		printf("Listening on port %s\n", port);
		return sock;
    }
    // Turn the socket into a listening socket if TCP
    ret = listen(sock, LISTEN_QUEUE_SIZE);
    if (ret == -1){
		perror("listen");
		close(sock);
		exit(EXIT_FAILURE);
    }

    printf("Listening on port %s\n", port);
    return sock;
}

void serve_udp(unsigned short port)
{
    /* 
	 * Listen for and respond to DNS requests over UDP.
	 * Initialize the cache.  Initialize the socket.  Receive datagram
	 * requests over UDP, and return the appropriate responses to the
	 * client.
	 *
	 * INPUT:  port: a numerical port on which the server should listen.
	 */
    int sock = create_udp_server_socket("8080", SOCK_DGRAM);
    printf("Created socket!\n");
    unsigned char message[BUFFER_MAX];
    char client_hostname[NI_MAXHOST];
    char client_port[NI_MAXSERV];
    while (1)
    {
		struct sockaddr_storage client_addr;
		int msg_length;
		socklen_t client_addr_len = sizeof(client_addr);

		// Receive a message from a client
		if ((msg_length = recvfrom(sock, message, BUFFER_MAX, 0, (struct sockaddr *)&client_addr, &client_addr_len)) < 0)
		{
			fprintf(stderr, "Failed in recvfrom\n");
			continue;
		}

		// Get and print the address of the peer (for fun)
		int ret = getnameinfo((struct sockaddr *)&client_addr, client_addr_len,
					client_hostname, BUFFER_MAX, client_port, BUFFER_MAX, 0);
		if (ret != 0)
		{
			fprintf(stderr, "Failed in getnameinfo: %s\n", gai_strerror(ret));
		}
		printf("Got a message from %s:%s\n", client_hostname, client_port);

		// Print the dns request by bytes.
		printf("\nMessage: ");
		print_bytes(&(message[0]), msg_length);

		unsigned char response[MAX_ENTRIES];
		int responseLength = get_response(message, msg_length, &(response[0]));

		// print_bytes(response, responseLength);

		sendto(sock, response, responseLength, 0, (struct sockaddr *)&client_addr, client_addr_len);
    }
    // return 0;
}

void serve_tcp(unsigned short port)
{
    /* 
	 * Listen for and respond to DNS requests over TCP.
	 * Initialize the cache.  Initialize the socket.  Receive requests
	 * requests over TCP, ensuring that the entire request is received, and
	 * return the appropriate responses to the client, ensuring that the
	 * entire response is transmitted.
	 *
	 * Note that for requests, the first two bytes (a 16-bit (unsigned
	 * short) integer in network byte order) read indicate the size (bytes)
	 * of the DNS request.  The actual request doesn't start until after
	 * those two bytes.  For the responses, you will need to similarly send
	 * the size in two bytes before sending the actual DNS response.
	 *
	 * In both cases you will want to loop until you have sent or received
	 * the entire message.
	 *
	 * INPUT:  port: a numerical port on which the server should listen.
	 */
}

int main(int argc, char *argv[])
{
    unsigned short port;
    int argindex = 1, daemonize = 0;
    if (argc < 3)
    {
		fprintf(stderr, "Usage: %s [-d] <cache file> <port>\n", argv[0]);
		exit(1);
    }
    if (argc > 3)
    {
		if (strcmp(argv[1], "-d") != 0)
		{
			fprintf(stderr, "Usage: %s [-d] <cache file> <port>\n", argv[0]);
			exit(1);
		}
		argindex++;
		daemonize = 1;
    }
	if(daemonize){
		pid_t pid = fork();
		if(pid < 0){
			printf("Fork failed\n");
			exit(1);
		} else if(pid != 0){
			printf("Daemon started with PID %d\n", pid);
			// Exit the parent process.
			raise(SIGCONT);
			raise(SIGINT); 
		} else if(pid == 0){
			// printf("Closing files in child\n");
			close(0);
			close(1);
			close(2);
		}
	}
		
	cachedb_file = argv[argindex++];
	port = atoi(argv[argindex]);

	init_db();
	serve_udp(8080);
    // daemonize, if specified, and start server(s)
    // ...
}
