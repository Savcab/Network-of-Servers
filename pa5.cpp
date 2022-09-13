/* C++ standard include files first */
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <map>
#include <thread>
#include <sys/time.h>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <sstream>
#include <algorithm>
#include <utility>
#include <atomic>

using namespace std;

/* C system include files next */
#include <arpa/inet.h>
#include <netdb.h>

/* C standard include files next */
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* your own include last */
#include "resources/my_socket.h"
#include "resources/my_readwrite.h"
#include "resources/md5-calc.h"
#include "resources/my_timestamp.h"
#include "resources/util.h"

/**
 * For storing messages for sending between reading and writing clients
 * */
class Message{
    public:
    string protocol;
    string type;
    map<string, string> headerData;
    string content;
    Message(string firstLine, map<string, string> hData){
        vector<string> components = better_split(firstLine, ' ');
        if(components.size() == 2){
            protocol = trimAll(components[0]);
            type = trimAll(components[1]);
        } else {
            protocol = "";
            type = "";
        }
        headerData = hData;
        content = "";
    }
    // similar to copy constructor
    Message(shared_ptr<Message> other){
        protocol = other->protocol;
        type = other->type;
        headerData = other->headerData;
        content = other->content;
    }
    string findType(){
        if(protocol.size() == 0 || type.size() == 0){
            return "";
        }
        // SAYHELLO
        if(protocol == "353NET/1.0" && type == "SAYHELLO"){
            return "SAYHELLO";
        }
        // LSUPDATE
        if(protocol == "353NET/1.0" && type == "LSUPDATE"){
            return "LSUPDATE";
        }
        // UCASTAPP
        if(protocol == "353NET/1.0" && type == "UCASTAPP"){
            return "UCASTAPP";
        }
        return "";
    }
};


/**
 * This class is for UDT Messges
 * */
class UDTMessage{
    public:
    string protocol;
    string type;
    string sesid;
    string to;
    string from;

    UDTMessage(string p, string t, string s){
        protocol = p;
        type = t;
        sesid = s;
    }
    UDTMessage(string line){
        vector<string> line_vec = better_split(line, ' ');
        if(line_vec.size() >= 3){
            protocol = line_vec[0];
            type = line_vec[1];
            sesid = line_vec[2];
        }
    }
    UDTMessage(shared_ptr<Message> msg){
        vector<string> line_vec = better_split(msg->content, ' ');
        if(line_vec.size() >= 3){
            protocol = line_vec[0];
            type = line_vec[1];
            sesid = line_vec[2];
        }
        to = msg->headerData["To"];
        from = msg->headerData["From"];
    }
    string toString(){
        string ans = "";
        ans += (protocol + " " + type + " " + sesid);
        return ans;
    }
    string findType(){
        if(protocol == "353UDT/1.0" && type == "PING"){
            return "PING";
        } else if(protocol == "353UDT/1.0" && type == "PONG"){
            return "PONG";
        } else if(protocol == "353UDT/1.0" && type == "TTLZERO"){
            return "TTLZERO";
        } else {
            return "";
        }
    }
};

/**
 * This is a class for storing connection data
 */
class Connection{
    public:
    // The number of this connection. -1 means not connected properly
    int conn_number;
    // The socket_fd of this connection. -1 means inactive/closed
    int socket_fd;
    // The orginal socket before this connection is closed
    int og_socket_fd;
    // Number of KB sent
    int bytes_sent;
    // the size of the file
    int response_body_size;
    // The shared pointers of the corresponding thread
    shared_ptr<thread> read_thread_ptr;
    shared_ptr<thread> write_thread_ptr;
    // For socket-reading thread to send work to corresponding socket-writing thread
    shared_ptr<mutex> m2;
    shared_ptr<condition_variable> cv;
    queue<shared_ptr<Message>> q;
    // For keeping track of the neighbor
    string neighbor_nodeid;

    Connection() : conn_number(-1), socket_fd(-1), og_socket_fd(-1), bytes_sent(0), response_body_size(0), read_thread_ptr(NULL), write_thread_ptr(NULL), m2(make_shared<mutex>()), cv(make_shared<condition_variable>()), q(queue<shared_ptr<Message>>()), neighbor_nodeid("") {}
    Connection(int c, int s, shared_ptr<thread> read, shared_ptr<thread> write){
        conn_number = c;
        socket_fd = s;
        og_socket_fd = socket_fd;
        bytes_sent = 0;
        read_thread_ptr = read;
        write_thread_ptr = write;
        m2 = make_shared<mutex>();
        cv = make_shared<condition_variable>();
        q = queue<shared_ptr<Message>>();
        response_body_size = 0;
        neighbor_nodeid = "";
    }
    // adds work to the work queue
    void add_work(shared_ptr<Message> msg){
        unique_lock<mutex> lock(*m2);
        q.push(msg);
        cv->notify_all();
    }
    // get the first available work
    shared_ptr<Message> get_work(){
        unique_lock<mutex> lock(*m2);
        shared_ptr<Message> msg;
        while(q.empty()){
            cv->wait(lock);
        }
        msg = q.front();
        q.pop();
        return msg;
    }
};

// GLOBAL VARIABLES
static int master_socket_fd = (-1); /* there is nothing wrong with using a global variable */
static int gnDebug = 0; /* change it to 0 if you don't want debugging messages */
static vector<shared_ptr<Connection>> connections; //keeps track of all the connection data 
static vector<string> active_neighbors; //keeps track of the nodeID of all the active neighbors
static int next_connection_number = 1;
static ostream *mylog(NULL);
static ofstream myFile;
static mutex m;
static condition_variable cv;
static queue<shared_ptr<Connection>> reaper_work_queue;
static string nodeID;
static map<string, map<string, string>> configData;
static map<string, shared_ptr<Message>> msg_cache;
static map<string, shared_ptr<Message>> adj_list;
static map<string, string> forwarding_table;
static int curr_sesid = 0;
static int next_sesid = 1;
// this variable is used for debugging
bool DEBUG = false;


// blueprints
void reaper_add_work(shared_ptr<Connection> w);
shared_ptr<Connection> make_new_connection(int socket_fd);
shared_ptr<Message> create_sayhello();
void initiate_lsupdate_flood();
void clean_adj_list();
void GetObjID(string node_id, const char *obj_category, string& hexstring_of_unique_obj_id, string& origin_start_time);
void add_to_cache(shared_ptr<Message> msg);
void log_write(ostream &log, shared_ptr<Connection> connection, string message);
void log_message(shared_ptr<Message> msg, string id, string category);
void update_forwarding_table();
void udt_send(shared_ptr<Message> msg);
shared_ptr<Message> create_ping(string sesid, int ttl, string destNodeID);
shared_ptr<Message> create_pong(string sesid, string destNodeID);
shared_ptr<Message> create_ttlzero(string sesid, string destNodeID);
void tr_add_event(shared_ptr<UDTMessage> msg);
shared_ptr<UDTMessage> tr_wait_for_event();
void traceroute(string dest_id, int sesid);

/* The following are functions for the server */
static
void usage()
{
    cerr << "usage: \"./pa2 CONFIGFILE\" to run as a server " << endl;
    cerr << "or \"./pa2 -c HOST PORT URI_1 OUTPUTFILE_1 [URI_2 OUTPUTFILE_2 ... URI_k OUTPUTFILE_k]\" to run as a client" << endl;
    exit(-1);
}

/**
 * This is the function you need to change to change the behavior of your server!
 * Returns non-zero if succeeds.
 * Otherwise, return 0;
 *
 * @param argc - number of arguments in argv.
 * @param argv - array of argc C-strings, must only use array index >= 0 and < argc.
 */
static
void process_options(int argc, char *argv[])
{
    if (gnDebug) {
        for (int i=0; i < argc; i++) {
            cerr << "[DBG-SVR]\targv[" << i << "]: '" << argv[i] << "'" << endl;
        }
    }
    if ((argc != 2) && ((string)argv[1] != "-c" && argc%2 == 0 && argc > 4)){
	    usage();
    }
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Helper function to write a message out of a log
 * */
void log_write(ostream &log, shared_ptr<Connection> connection, string message){
    if(DEBUG) cout << "M LOCKED 182a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 182b\n";
    log << "[" << get_timestamp_now() << "] [" << connection->conn_number << "] " << message << "\n";
    log.flush();
    m.unlock();
}

/**
 * Version 2 of helper function
 * */
void log_write(ostream &log, string message){
    if(DEBUG) cout << "M LOCKED 192a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 192\n";
    log << "[" << get_timestamp_now() << "] " << message << "\n";
    log.flush();
    m.unlock();
}


/**
 * Function for reading thread
 * */
static void read_from_client(shared_ptr<Connection> connection){
    // get the lock
    m.lock();
    if(DEBUG) cout << "M LOCKED 205\n";
    m.unlock();
    string line;
    string firstLine;
    // reading the first line of the header break if line is empty or reading error
    int bytes_received = read_a_line(connection->socket_fd, line);
    // if have error reading
    if(bytes_received <= 0){
        m.lock();
        if(DEBUG) cout << "M LOCKED 215\n";
        if(connection->socket_fd >= 0){
            shutdown(connection->socket_fd, SHUT_RDWR);
        }
        connection->socket_fd = -1;
        m.unlock();
        connection->add_work(nullptr);
        connection->write_thread_ptr->join();
        reaper_add_work(connection);
        return;
    }
    firstLine = line;
    // reading the rest of the header and put the data in a message
    map<string, string> headerData;
    parseHeader(connection->socket_fd, headerData, *mylog);
    shared_ptr<Message> received_message = make_shared<Message>(firstLine, headerData);

    string from_nodeid = trimAll(headerData["From"]);
    // log SAYHELLO
    log_message(received_message, from_nodeid, "r");

    // add neighbor if neighbor not duplicate
    bool duplicate = false;
    if(connection->neighbor_nodeid == ""){
        m.lock();
        if(DEBUG) cout << "M LOCKED 240\n";
        for(auto conn_ptr : connections){
            if(conn_ptr != connection && conn_ptr->neighbor_nodeid != "" && conn_ptr->neighbor_nodeid == from_nodeid){
                duplicate = true;
            }
        }
        m.unlock();
        if(!duplicate){
            m.lock();
            if(DEBUG) cout << "M LOCKED 250\n";
            connection->neighbor_nodeid = from_nodeid;
            m.unlock();
            // create work SAYHELLO if received message is SAYHELLO
            if(received_message->findType() == "SAYHELLO"){
                shared_ptr<Message> sayhello = create_sayhello();
                connection->add_work(sayhello);
                // logging
                log_message(sayhello, connection->neighbor_nodeid, "i");
            }
        }
    } else {
        m.lock();
        if(DEBUG) cout << "M LOCKED 261\n";
        for(auto conn_ptr : connections){
            if(conn_ptr != connection && conn_ptr->neighbor_nodeid != "" && conn_ptr->neighbor_nodeid == from_nodeid){
                duplicate = true;
            }
        }
        m.unlock();
    }

    if(!duplicate){
        // declare this node as an active neighbor
        m.lock();
        if(DEBUG) cout << "M LOCKED 274\n";
        active_neighbors.push_back(from_nodeid);
        m.unlock();
        initiate_lsupdate_flood(); //flood when gained an active neighbor
        
        // maintain a connection with this neighbor
        for(;;){
            // read message from c.socket
            bytes_received = read_a_line(connection->socket_fd, line);
            if(bytes_received <= 0) break;
            // log it out for debugging
            if(DEBUG){
                m.lock();
                *mylog << "\n\n\t" << line;
                mylog->flush();
                m.unlock();
            }
            map<string, string> msg_header_data;
            parseHeader(connection->socket_fd, msg_header_data, *mylog);
            shared_ptr<Message> msg = make_shared<Message>(line, msg_header_data);
            // reading the content
            int cont_length = stoi(msg_header_data["Content-Length"]);
            char *buffer = new char[cont_length]; 
            read(connection->socket_fd, buffer, cont_length);
            msg->content = (string)buffer;
            // log it out for debugging
            if(DEBUG){
                m.lock();
                *mylog << msg->content << "\n";
                mylog->flush();
                m.unlock();
            }
            delete [] buffer;

            // logging
            log_message(msg, from_nodeid, "r");

            // stop when program ends
            if(DEBUG) cout << "M LOCKED 287a\n";
            m.lock();
            if(DEBUG) cout << "M LOCKED 287b\n";
            if(master_socket_fd == -1 || bytes_received <= 0){
                m.unlock();
                break;
            }
            m.unlock();
            

            // process diferent kinds of message differently
            // process LSUPDATE
            if(msg->findType() == "LSUPDATE"){

                m.lock();
                string orig_node_id = msg->headerData["From"];
                // flood if node is 2 hops away and it's either a new node or a node that went offline and is back up again
                if((orig_node_id != nodeID && count(active_neighbors.begin(), active_neighbors.end(), orig_node_id) == 0) && ((adj_list.count(orig_node_id) != 0 && adj_list[orig_node_id]->headerData["OriginStartTime"] != msg->headerData["OriginStartTime"]) || (adj_list.count(orig_node_id) == 0))){
                    m.unlock();
                    initiate_lsupdate_flood();
                    m.lock();
                }
                if(orig_node_id != nodeID){
                    adj_list[orig_node_id] = msg;
                }
                m.unlock();

                // act as a router haven't seen this message in the cache before
                m.lock();
                if(msg_cache.count(msg->headerData["MessageID"]) == 0){
                    add_to_cache(msg);
                    if(msg->headerData["Flood"] == "1"){
                        // make a copy of msg
                        shared_ptr<Message> send = make_shared<Message>(msg);
                        int ttl = stoi(send->headerData["TTL"]);
                        ttl -= 1;
                        if(ttl > 0){
                            send->headerData["TTL"] = to_string(ttl);
                            for(auto conn_ptr : connections){
                                conn_ptr->add_work(send);
                                // logging
                                m.unlock();
                                log_message(msg, conn_ptr->neighbor_nodeid, "d");
                                m.lock();
                            }
                        }
                    }
                }
                m.unlock();
            // process UCASTAPP
            } else if(msg->findType() == "UCASTAPP"){
                // if this node is the destination node
                if(nodeID == msg->headerData["To"]){
                    shared_ptr<UDTMessage> udtmsg = make_shared<UDTMessage>(msg);
                    // if msg is PING
                    if(udtmsg->findType() == "PING"){
                        shared_ptr<Message> pong = create_pong(udtmsg->sesid, msg->headerData["From"]);
                        udt_send(pong);
                    // if msg is PONG
                    }else if(udtmsg->findType() == "PONG" && udtmsg->sesid == to_string(curr_sesid)){
                        // assuming PONG will only be sent by other machines responding to this machine's PING
                        tr_add_event(udtmsg);
                    // if msg is TTLZERO
                    } else if(udtmsg->findType() == "TTLZERO" && udtmsg->sesid == to_string(curr_sesid)){
                        // assuming TTLZ will only be sent by other machines responding to this machine's failed PING
                        tr_add_event(udtmsg);
                    }
                    // string nl = "";
                    // if(msg->headerData["Next-Layer"] == "1"){ nl = "UDT";} else if(msg->headerData["Next-Layer"] == "2"){ nl = "RDT";}
                    // cout << "No receiver for " << nl << " message " << msg->content << " from " << msg->headerData["From"] << "\n";
                // if not, forward this message according to forwarding_table
                } else {
                    clean_adj_list();
                    update_forwarding_table();
                    // make of copy of msg
                    shared_ptr<Message> send = make_shared<Message>(msg);
                    int ttl = stoi(send->headerData["TTL"]);
                    ttl -= 1;
                    if(ttl > 0){
                        // forward this message according to forwarding_table
                        string nextHopID = forwarding_table[send->headerData["To"]];
                        send->headerData["TTL"] = to_string(ttl);
                        m.lock();
                        for(auto conn_ptr : connections){
                            if(conn_ptr->neighbor_nodeid == nextHopID){
                                conn_ptr->add_work(send);
                                // logging
                                m.unlock();
                                log_message(msg, conn_ptr->neighbor_nodeid, "f");
                                m.lock();
                            }
                        }
                        m.unlock();
                    // if ttl = 0 AND it is a ping message, must reply with a TTLZERO message
                    } else if(better_split(msg->content, ' ')[1] == "PING"){
                        shared_ptr<Message> ttlzero = create_ttlzero(better_split(msg->content, ' ')[2], msg->headerData["From"]);
                        udt_send(ttlzero);
                    }
                }
            }
        }
    }

    // cleaning up
    if(DEBUG) cout << "M LOCKED 299a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 299b\n";
    if(connection->socket_fd >= 0){
        shutdown(connection->socket_fd, SHUT_RDWR);
    }
    connection->socket_fd = -1;
    m.unlock();
    // signal socket-writing thread to self-terminate
    connection->add_work(nullptr);
    // join with the socket-writing thread
    connection->write_thread_ptr->join();
    // add dead connection to the reaper work queue
    reaper_add_work(connection);
}

/**
 * Function for writing thread 
 * */
static void write_to_client(shared_ptr<Connection> connection){
    if(DEBUG) cout << "M LOCKED 319a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 319b\n";
    m.unlock();
    for(;;){
        shared_ptr<Message> work = connection->get_work();
        // if need to stop infinite loop
        if(work == nullptr || connection->socket_fd < 0){
            break;
        }

        // send the message
        vector<string> response;
        // send the header
        response.push_back(work->protocol + " " + work->type + "\r\n");
        for(const auto &item : work->headerData){
            response.push_back(item.first + ": " + item.second + "\r\n");
        }
        response.push_back("\r\n");
        for(int i = 0 ; i < (int)response.size() ; i++){
            better_write(connection->socket_fd, response[i].c_str(), response[i].length());
            if(DEBUG){cout << "\t" << response[i];}
        }
        // log content out for debugging
        if(DEBUG){cout << "\t" << work->content << "\n";}

        // send the body
        if(work->content != ""){
            better_write(connection->socket_fd, work->content.c_str(), (int)sizeof(work->content));
        }
        
        // check to see if socket is closed by command
        if(DEBUG) cout << "M LOCKED 346a\n";
        m.lock();
        if(DEBUG) cout << "M LOCKED 346b\n";
        if(connection->socket_fd==-2 || master_socket_fd==-1){
            m.unlock();
            log_write(*mylog, connection, "Connection closed");
            close(connection->og_socket_fd);
            connection->socket_fd = -1;
            break;
        }
        m.unlock();
    }
    log_write(*mylog, connection, "Socket-writing thread has terminated");
}

/**
 * Function for maintaining neighbors thread
 **/
static void neighborThread(){
    // read configData
    // compile a list of neighbors
    vector<string> all_neighbors = better_split(configData["topology"][nodeID], ',');

    for(;;){
        vector<string> inactive_neighbors = all_neighbors;
        // if socket closed, terminate
        if(DEBUG) cout << "M LOCKED 380a\n";
        m.lock();
        if(DEBUG) cout << "M LOCKED 380b\n";
        if(master_socket_fd == -1){
            m.unlock();
            break;
        } else {
        // remove all active neighbors from inactive_neighbors
            for(auto conn_ptr : connections){
                if(conn_ptr->neighbor_nodeid != ""){
                    auto itr = inactive_neighbors.begin();
                    while(itr != inactive_neighbors.end()){
                        if(*itr == conn_ptr->neighbor_nodeid){
                            break;
                        }
                        itr++;
                    }
                    if(itr != inactive_neighbors.end()){
                        inactive_neighbors.erase(itr);
                    }
                }
            }
        }
        m.unlock();

        // remake connection with all inactive neighbors and send SAYHELLO
        for(string neighbor_id : inactive_neighbors){
            string name = better_split(neighbor_id, ':')[0];
            string port = better_split(neighbor_id, ':')[1];
            int neigh_socket_fd = create_client_socket_and_connect(name, port);
            if(neigh_socket_fd >= 0){
                if(DEBUG) cout << "M LOCKED 411a\n";
                m.lock();
                if(DEBUG) cout << "M LOCKED 411b\n";
                shared_ptr<Connection> neigh_conn_ptr = make_new_connection(neigh_socket_fd);
                neigh_conn_ptr->neighbor_nodeid = neighbor_id;
                connections.push_back(neigh_conn_ptr);
                shared_ptr<Message> sayhello = create_sayhello();
                neigh_conn_ptr->add_work(sayhello);
                m.unlock();
                // logging
                log_message(sayhello, neigh_conn_ptr->neighbor_nodeid, "i");
            }
        }
        sleep(stoi(configData["params"]["neighbor_retry_interval"]));
    }
}

/* End of functions for the server */
/* The following are functions for the client */

/**
 * This function is not used since we are using getline().
 *
 * Note: just like getline(), the returned string does not contain a trailing '\n'.
 */
string readline()
{
    string message;
    for (;;) {
        char ch=(char)(cin.get() & 0xff);
        if (cin.fail()) {
            break;
        }
        if (ch == '\n') {
            break;
        }
        message += ch;
    }
    return message;
}

/**
 * Helper function to create a SAYHELLO message
 * */
shared_ptr<Message> create_sayhello(){
    map<string, string> messageData;
    messageData["TTL"] = "1";
    messageData["Flood"] = "0";
    messageData["From"] = nodeID;
    messageData["Content-Length"] = "0";
    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 SAYHELLO", messageData);
    return msg;
}

/**
 * Helper functino to create a LSUPDATE message
 * */
shared_ptr<Message> create_lsupdate(){
    // content
    m.lock();
    string cont = "";
    for(int i = 0 ; i < (int)active_neighbors.size() ; i++){
        cont += active_neighbors[i];
        if(i < (int)active_neighbors.size()-1){
            cont += ",";
        }
    }
    m.unlock();

    // header
    map<string, string> messageData;
    string hash; string time;
    m.lock();
    GetObjID(nodeID, nullptr, hash, time);
    m.unlock();
    messageData["TTL"] = configData["params"]["max_ttl"];
    messageData["Flood"] = "1";
    messageData["MessageID"] = hash;
    messageData["From"] = nodeID;
    messageData["OriginStartTime"] = time;
    messageData["Content-Length"] = to_string(sizeof(cont));

    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 LSUPDATE", messageData);
    msg->content = cont;
    return msg;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Helper function to create a UCASTAPP message 
 **/
shared_ptr<Message> create_ucastapp(string destNodeID, string msgBody){
    // content
    string cont = msgBody;

    // header
    map<string, string> messageData;
    string hash; string time;
    m.lock();
    GetObjID(nodeID, nullptr, hash, time);
    m.unlock();
    messageData["TTL"] = configData["params"]["max_ttl"];
    messageData["Flood"] = "0";
    messageData["MessageID"] = hash;
    messageData["From"] = nodeID;
    messageData["To"] = destNodeID;
    messageData["Next-Layer"] = "1"; //LAB12 FOR UDT
    messageData["Content-Length"] = to_string(sizeof(cont));

    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 UCASTAPP", messageData);
    msg->content = cont;
    return msg;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Cause a whole lsupdate flood in the network
 * */

void initiate_lsupdate_flood(){
    shared_ptr<Message> lsupdate = create_lsupdate();
    add_to_cache(lsupdate);
    m.lock();
    for(auto conn_ptr : connections){
        conn_ptr->add_work(lsupdate);
        m.unlock();
        // logging
        log_message(lsupdate, conn_ptr->neighbor_nodeid, "i");
        m.lock();
    }
    m.unlock();
}

/**
 * The function to use for HTTP communication
 */
static 
void send_HTTP_request(int client_socket_fd, string uri, string ofile, string host, string port){
    vector<string> http_req_lines;
    http_req_lines.push_back("GET " + uri + " HTTP/1.1\r\n");
    http_req_lines.push_back("User-Agent: lab4b\r\n");
    http_req_lines.push_back("Accept: */*\r\n");
    http_req_lines.push_back("Host: " + host + ":" + port + "\r\n");
    http_req_lines.push_back("\r\n");
    //start sending the data to the server and printing it out
    for(int i = 0 ; i < (int)http_req_lines.size() ; i++){
        better_write(client_socket_fd, http_req_lines[i].c_str(), http_req_lines[i].length());
        cout << "\t" << http_req_lines[i];
    }
    //reading the response header
    string line;
    string http_version;
    bool error = false;
    map<string, string> header_kv;
    int cnt = 0;
    for(;;){
        read_a_line(client_socket_fd, line);
        cout << '\t' << line;
        //if the line is the empty line at the end of header
        if(line.size() == 2 && (line[0] == '\r') && (line[1]=='\n')){
            break;
        }
        //for the first line
        if(cnt==0){
            vector<string> firstLine = split(line, ' ');
            http_version = firstLine[0];
        } else {
            vector<string> key_value = split(line, ':');
            string key = key_value[0];
            //convert all key to lower since case insensitive
            for(int i = 0 ; i < (int)key.length() ; i++){
                key[i] = tolower(key[i]);
            }
            //ignore first char since it will be whitespace
            header_kv[key] = key_value[1].substr(1, key_value[1].length()-1);
        }
        cnt++;
    }
    //check to see if content-length exists
    if(header_kv.count("content-length") == 0){
        error = true;
    }
    if(error){
        cout << "Program has received an ERROR in the response message, aborting program." << endl;
        shutdown(client_socket_fd, SHUT_RDWR);
        close(client_socket_fd);
        return;
    }
    int cont_len = stoi(header_kv["content-length"]);
    //creating the new file write content-length data to it
    int ofile_fd = creat(ofile.c_str(), O_WRONLY);
    int chunk_size = 1024;
    // if the cont_len is less than 1024, just read the content length
    if(cont_len < chunk_size){ 
        chunk_size = cont_len;
    }
    char buffer [chunk_size];
    int bytes_read;
    // write till it is long enough
    do{
        bytes_read = read(client_socket_fd, buffer, chunk_size);
        write(ofile_fd, buffer, bytes_read);
        cont_len -= bytes_read;
    } while(cont_len > 0);
    //close ofile
    shutdown(ofile_fd, SHUT_WR);
    close(ofile_fd);
}

/**
 * Function for command console for the server
 */
void serverConsole(){
    for(;;){
        cout << nodeID << "> ";
        string command;
        getline(cin, command);
        if(command == "neighbors"){
            if(DEBUG) cout << "M LOCKED 549a\n";
            m.lock();
            if(DEBUG) cout << "M LOCKED 549b\n";
            int cnt = 0;
            string temp = "";
            for(int i = 0 ; i < (int)active_neighbors.size(); i++){
                cnt++;
                temp += active_neighbors[i];
                if(i < (int)active_neighbors.size()-1){
                    temp+= ",";
                }
            }
            if(cnt == 0){
                cout << nodeID << " has no active neighbors\n";
            } else {
                cout << "Active neighbors of " << nodeID << ":\n";
                cout << "\t" << temp << "\n";
            }
            m.unlock();
        }else if(command == "quit"){
            shared_ptr<Connection> c = make_shared<Connection>(Connection());
            reaper_add_work(c);
            break;
        }else if(command == "netgraph"){
            clean_adj_list();
            m.lock();
            if(active_neighbors.size() == 0){
                cout << nodeID << " has no active neighbors\n";
            } else {
                // print out own neighbor list first
                cout << nodeID << ": ";
                for(int i = 0 ; i < (int)active_neighbors.size() ; i++){
                    cout << active_neighbors[i];
                    if(i < (int)active_neighbors.size()-1){
                        cout << ",";
                    }
                }
                cout << "\n";

                // print out neighbor list for all other reachable nodes
                for(const auto &item : adj_list){
                    cout << item.first << ": " << item.second->content << "\n";
                }
            } 
            m.unlock();
        } else if(command == "forwarding"){
            update_forwarding_table();
            // print out forwarding table
            m.lock();
            if(forwarding_table.size() == 1){
                cout << nodeID << " has an empty forwarding table\n";
            } else {
                for(const auto &item : forwarding_table){
                    cout << item.first << ": " << item.second << "\n";
                }
            }
            m.unlock();
        } else if(command.substr(0, 7) == "udtsend"){
            clean_adj_list();
            update_forwarding_table();
            vector<string> command_vec = better_split(command, ' ');
            // if the format for udtsend is correct
            if(command_vec.size() >= 2){
                string target = command_vec[1];
                string data = "";
                for(int i = 2 ; i < (int)command_vec.size() ; i++){
                    data += command_vec[i];
                    if(i < (int)command_vec.size()-1){
                        data += " ";
                    }
                }
                string printout;
                if(target == nodeID){
                    printout = "Cannot use udtsend command to send message to yourself.\n";
                } else if(adj_list.count(target) == 0){
                    printout = target + " is not reachable\n";
                } else {
                    printout = "";
                }
                shared_ptr<Message> msg = create_ucastapp(target, data);
                udt_send(msg);
                cout << printout;
            }
        } else if(command.substr(0, 10) == "traceroute"){
            clean_adj_list();
            update_forwarding_table();
            vector<string> command_vec = better_split(command, ' ');
            // if the format for traceroute is correct
            if(command_vec.size() == 2){
                string dest_id = command_vec[1];
                if(dest_id == nodeID){
                    cout << "Cannot traceroute to youself.\n";
                } else {
                    curr_sesid = next_sesid;
                    traceroute(dest_id, curr_sesid);
                    next_sesid++;
                }

            }
        } else {
            cout << "Command not recognized. Valid commands are:     Command not recognized.  Valid commands are:\n\techoapp target\n\tforwarding\n\tneighbors\n\tnetgraph\n\trdtsend target message\n\ttraceroute target\n\tquit\n";
        }
    }
    if(DEBUG) cout << "M LOCKED 576a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 576b\n";
    shutdown(master_socket_fd, SHUT_RDWR);
    close(master_socket_fd);
    master_socket_fd = -1;
    for(auto conn_ptr : connections){
        if(conn_ptr->socket_fd >= 0){
            shutdown(conn_ptr->socket_fd, SHUT_RDWR);
            conn_ptr->socket_fd = -2;
        }
    }
    m.unlock();
}
/**
 * Function to add work for the reaper thread
 * */
void reaper_add_work(shared_ptr<Connection> w) {
    if(DEBUG) cout << "M LOCKED 594a\n";
    m.lock();
    if(DEBUG) cout << "M LOCKED 594b\n";
    reaper_work_queue.push(w);
    cv.notify_all();
    m.unlock();
}

/**
 * Function for the reaper thread to wait for work
 */
shared_ptr<Connection> reaper_wait_for_work() {
  unique_lock<mutex> l(m);

  while (reaper_work_queue.empty()) {
    cv.wait(l);
  }
  shared_ptr<Connection> k = reaper_work_queue.front();
  reaper_work_queue.pop();

  return k;
}

/**
 * Reaper thread function
 */
void reaperThread(){
    for(;;){
        shared_ptr<Connection> c = reaper_wait_for_work();
        // check if c is "NULL"
        if(c->read_thread_ptr == NULL){
            break;
        } else {
            c->read_thread_ptr->join();
            log_write(*mylog, c, "\tReaper thread has joined with socket reading thread");
            close(c->og_socket_fd);
            if(DEBUG) cout << "M LOCKED 631a\n";
            m.lock();
            if(DEBUG) cout << "M LOCKED 631b\n";
            // erase the connection from connections list
            for(auto itr = connections.begin() ; itr != connections.end() ;){
                shared_ptr<Connection> conn_ptr = (*itr);
                if(conn_ptr == c){
                    itr = connections.erase(itr);
                } else {
                    itr++;
                }
            }
            // erase the neighbor_id from active_neighbors
            for(auto itr = active_neighbors.begin() ; itr != active_neighbors.end() ; ){
                if((*itr) == c->neighbor_nodeid){
                    itr = active_neighbors.erase(itr);
                } else {
                    itr++;
                }
            }
            m.unlock();
            initiate_lsupdate_flood(); //flood when loses an active neighbor
        }
    }

    // reaper joins with every thread on the list
    for(;;){
        if(DEBUG) cout << "M LOCKED 648a\n";
        m.lock();
        if(DEBUG) cout << "M LOCKED 648b\n";
        if(connections.empty()){
            m.unlock();
            break;
        }
        for(auto itr = connections.begin() ; itr != connections.end() ;){
            shared_ptr<Connection> c = (*itr);
            m.unlock();
            c->read_thread_ptr->join();
            log_write(*mylog, c, "\tReaper thread has joined with socket reading thread");
            if(DEBUG) cout << "M LOCKED 660a\n";
            m.lock();
            if(DEBUG) cout << "M LOCKED 660b\n";
            close(c->og_socket_fd);
            itr = connections.erase(itr);
            m.unlock();
        }
    }
}

/**
 * helper function to make a new connection
 * */
shared_ptr<Connection> make_new_connection(int socket_fd){
    shared_ptr<Connection> c = make_shared<Connection>(Connection(next_connection_number, socket_fd, NULL, NULL));
    shared_ptr<thread> read = make_shared<thread>(thread(read_from_client, c));
    shared_ptr<thread> write = make_shared<thread>(thread(write_to_client, c));
    c->read_thread_ptr = read;
    c->write_thread_ptr = write;
    next_connection_number++;
    return c;
}

/**
 * Required function
 * */
void GetObjID(string node_id, const char *obj_category, string& hexstring_of_unique_obj_id, string& origin_start_time){
    
    static unsigned long seq_no = 1L;
    static int first_time = 1;
    static struct timeval node_start_time;
    static char origin_start_time_buf[18];
    char hexstringbuf_of_unique_obj_id[80];

    /* IMPORTANT: this code must execute inside a critical section of the main mutex */
    if (first_time) {
        gettimeofday(&node_start_time, NULL);
        snprintf(origin_start_time_buf, sizeof(origin_start_time_buf),
                "%010d.%06d", (int)(node_start_time.tv_sec), (int)(node_start_time.tv_usec));
        first_time = 0;
    }
    seq_no++;
    struct timeval now;
    gettimeofday(&now, NULL);
    snprintf(hexstringbuf_of_unique_obj_id, sizeof(hexstringbuf_of_unique_obj_id),
            "%s_%1ld_%010d.%06d", node_id.c_str(), (long)seq_no, (int)(now.tv_sec), (int)(now.tv_usec));
    hexstring_of_unique_obj_id = hexstringbuf_of_unique_obj_id;
    origin_start_time = origin_start_time_buf;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Helper function, runs bfs 
 *      The returned map has the name of the id as the key 
 *      Value is pair<int, string> 
 *          The int is the distance of that node to this node
 *          The string is the predecessor node_id for that node
 * */
map<string, pair<int, string>> bfs(){
    // create a full graph adj list(map<string, vector<string>>) using adj_list where each string is a node_id
    map<string, vector<string>> full_graph;
    m.lock();
    for(const auto &item : adj_list){
        full_graph[item.first] = better_split(item.second->content, ',');
    }
    full_graph[nodeID] = active_neighbors;
    m.unlock();

    // run bfs on full_graph
	queue<string> bfs_q;
	map<string, pair<int, string>> dist;
	string start = nodeID;
	bfs_q.push(start);
	int cnt = 0;
    dist[start] = pair<int, string>(cnt, "");
	while(!bfs_q.empty()){
		string curr = bfs_q.front();
		bfs_q.pop();
		cnt = dist[curr].first;
        // for every neighbor, if not exist in dist already, add it to dist
		for(int i = 0 ; i < (int)full_graph[curr].size() ; i++){
			if(dist.count(full_graph[curr].at(i)) == 0){
				bfs_q.push(full_graph[curr].at(i));
                dist[full_graph[curr].at(i)] = pair<int, string>(cnt+1, curr);
			}
		}
	}
    return dist;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Function to update the forwarding table of this node
 * */
void update_forwarding_table(){
    // will completely replace the previous forwarding_table with this one
    map<string, string> new_forwarding_table;
    // call bfs
    map<string, pair<int, string>> dist = bfs();

    // update forwarding_table with bfs data
    for(const auto &item : dist){
        // find the next hop for this nodeID
        string nextHop = item.first;
        // if this is origin node
        if(item.second.second == ""){
            nextHop = nodeID;
        } else {
            string temp = item.second.second;
            while(temp != nodeID){
                // nextHop will always stay one step behind of temp
                nextHop = temp;
                temp = dist[temp].second;
            }
        }
        new_forwarding_table[item.first] = nextHop;
    }

    // replace the forwarding_table
    m.lock();
    forwarding_table = new_forwarding_table;
    m.unlock();
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Function to "clean" the adjacency list by running BFS and removing the nodes that are unreachable
 * */
void clean_adj_list(){
    // run bfs
    map<string, pair<int, string>> dist = bfs();

    // use dist to delete unreachable nodes from adj_list
    // make the unreachable list
    m.lock();
    vector<string> unreachable;
    for(const auto &item : adj_list){
        unreachable.push_back(item.first);
    }
    for(const auto &item : dist){
        auto itr = find(unreachable.begin(), unreachable.end(), item.first);
        if(itr != unreachable.end()){
            unreachable.erase(itr);
        }
    }
    // remove from adj_list based on unreachable
    for(auto itr = adj_list.begin() ; itr != adj_list.end() ; ){
        if(count(unreachable.begin(), unreachable.end(), itr->first) != 0){
            itr = adj_list.erase(itr);
        } else {
            itr++;
        }
    }
    m.unlock();
}

/**
 * Helper function - adds this message to the msg_cache
 * */
void add_to_cache(shared_ptr<Message> msg){
    msg_cache[msg->headerData["MessageID"]] = msg;
}

/**
 * Helper function to shorten a MessageID for logging
 * */
string shorten_message_id(string messageID){
    string ans = "";
    vector<string> id = better_split(messageID, '_');
    if((int)id.size() != 3) return "InvalidMessageID";
    ans += "{" + id[0] + "_" + id[1] + "}";
    return ans;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!!
 * Helper function - logging when sending or receiving 353NET protocol messages
 * category is either r, i, d, or f which means:
 *      r - receiving
 *      i - initiating
 *      d - due to flooding
 *      f - forwarding
 * id is the neighborID that needs to be given to this function in order to log
 * */
void log_message(shared_ptr<Message> msg, string id, string category){
    // Format: [TIMESTAMP] {r|i|d|f} MSGTYPE NEIGHBOR TTL FLOOD CONTENT_LENGTH msg-dependent-data
    // initate some variables
    ostream log(NULL);
    if(msg->findType() == "SAYHELLO"){
        if(configData["logging"]["SAYHELLO"] == "1"){
            log.rdbuf(mylog->rdbuf());
        } else if(configData["logging"]["SAYHELLO"] == "0") {
            log.rdbuf(cout.rdbuf());
        }
    } else if(msg->findType() == "LSUPDATE"){
        if(configData["logging"]["LSUPDATE"] == "1"){
            log.rdbuf(mylog->rdbuf());
        } else if(configData["logging"]["LSUPDATE"] == "0") {
            log.rdbuf(cout.rdbuf());
        }
    } else if(msg->findType() == "UCASTAPP"){
        if(configData["logging"]["UCASTAPP"] == "1"){
            log.rdbuf(mylog->rdbuf());
        } else if(configData["logging"]["UCASTAPP"] == "0") {
            log.rdbuf(cout.rdbuf());
        }
    } else {
        return;
    }
    string flood;
    if(msg->headerData["Flood"] == "1"){ flood = "F"; } else { flood = "-";}
    
    // forming the message
    string out;
    out = " " + category + " " + msg->findType() + " " + id + " " + msg->headerData["TTL"] + " " + flood + " " + msg->headerData["Content-Length"];
    
    // forming the message dependent data
    if(msg->findType() == "LSUPDATE"){
        string shorten_id = shorten_message_id(msg->headerData["MessageID"]);
        string msg_dep_data = " " + shorten_id + " " + msg->headerData["OriginStartTime"] + " " + msg->headerData["From"] + " " + msg->content;
        out += msg_dep_data;
    } else if(msg->findType() == "UCASTAPP"){
        string shorten_id = shorten_message_id(msg->headerData["MessageID"]);
        string msg_dep_data = " " + shorten_id + " " + msg->headerData["From"] + " " + msg->headerData["To"] + " " + msg->headerData["Next-Layer"] + " " + msg->content;
        out += msg_dep_data;
    }
    
    log_write(log, out);
}

/**
 * !!MUTEX MUST BE UNLOCKED WHEN CALLING THIS FUNCTION!!
 * This function is used to send a UCASTAPP msg t
 */
void udt_send(shared_ptr<Message> msg){
    // update forwarding table just in case
    update_forwarding_table();
    m.lock();
    string nextHopID = forwarding_table[msg->headerData["To"]];
    for(auto conn_ptr : connections){
        if(conn_ptr->neighbor_nodeid == nextHopID){
            conn_ptr->add_work(msg);
            // logging
            m.unlock();
            log_message(msg, conn_ptr->neighbor_nodeid, "i");
            m.lock();
        }
    }
    m.unlock();
}

// ------------------------------- traceroute applicaton section below ----------------------------- //

// GLOBAL VARIABLES for traceroute
static mutex m_tr; //mutex for traceroute
static condition_variable cv_tr; //cv for traceroute
static queue<shared_ptr<UDTMessage>> q_tr; //queue for traceroute
static atomic<bool> stop_timer(false); //variable to turn off timer

/**
 * makes a PING message
 * */
shared_ptr<Message> create_ping(string sesid, int ttl, string destNodeID){
    // content
    string cont = "353UDT/1.0 PING " + sesid;
    // header
    map<string, string> messageData;
    string hash; string time;
    m.lock();
    GetObjID(nodeID, nullptr, hash, time);
    m.unlock();
    messageData["TTL"] = to_string(ttl);
    messageData["Flood"] = "0";
    messageData["MessageID"] = hash;
    messageData["From"] = nodeID;
    messageData["To"] = destNodeID;
    messageData["Next-Layer"] = "1"; //LAB12 FOR UDT
    messageData["Content-Length"] = to_string(sizeof(cont));
    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 UCASTAPP", messageData);
    msg->content = cont;
    return msg;
}
/**
 * makes a PONG message
 */
shared_ptr<Message> create_pong(string sesid, string destNodeID){
    // content
    string cont = "353UDT/1.0 PONG " + sesid;
    // header
    map<string, string> messageData;
    string hash; string time;
    m.lock();
    GetObjID(nodeID, nullptr, hash, time);
    m.unlock();
    messageData["TTL"] = configData["params"]["max_ttl"];
    messageData["Flood"] = "0";
    messageData["MessageID"] = hash;
    messageData["From"] = nodeID;
    messageData["To"] = destNodeID;
    messageData["Next-Layer"] = "1"; //LAB12 FOR UDT
    messageData["Content-Length"] = to_string(sizeof(cont));
    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 UCASTAPP", messageData);
    msg->content = cont;
    return msg;
}
/**
 * makes a TTLZERO message
 */
shared_ptr<Message> create_ttlzero(string sesid, string destNodeID){
    // content
    string cont = "353UDT/1.0 TTLZERO " + sesid;
    // header
    map<string, string> messageData;
    string hash; string time;
    m.lock();
    GetObjID(nodeID, nullptr, hash, time);
    m.unlock();
    messageData["TTL"] = configData["params"]["max_ttl"];
    messageData["Flood"] = "0";
    messageData["MessageID"] = hash;
    messageData["From"] = nodeID;
    messageData["To"] = destNodeID;
    messageData["Next-Layer"] = "1"; //LAB12 FOR UDT
    messageData["Content-Length"] = to_string(sizeof(cont));
    shared_ptr<Message> msg = make_shared<Message>("353NET/1.0 UCASTAPP", messageData);
    msg->content = cont;
    return msg;
}

/**
 * Traceroute application wait for event
 * */
shared_ptr<UDTMessage> tr_wait_for_event(){
    unique_lock<mutex> lock(m_tr);
    shared_ptr<UDTMessage> msg;
    while(q_tr.empty()){
        cv_tr.wait(lock);
    }
    msg = q_tr.front();
    q_tr.pop();
    return msg;
}

/**
 * Traceroute application add event to queue
 */
void tr_add_event(shared_ptr<UDTMessage> msg){
    unique_lock<mutex> lock(m_tr);
    q_tr.push(msg);
    cv_tr.notify_all();
}

/**
 * Function for timer thread
 */
void timer_thread(int lifetime){
    bool timeout = false;
    // 1000000 microseconds in a second
    for(int i = 0 ; i < lifetime*100 ; i++){
        if(i == lifetime-1){
            timeout = true;
        }
        if(!stop_timer){
            usleep(10000);
        } else {
            break;
        }
    }
    if(timeout){
        tr_add_event(nullptr);
    }
    // reset stop_timer
    stop_timer = false;
}

/**
 * !!REQUIRES MUTEX TO BE FREE!! 
 * Traceroute application
 * */
void traceroute(string dest_id, int sesid){
    int max_ttl = stoi(configData["params"]["max_ttl"]);
    int msg_lifetime = stoi(configData["params"]["msg_lifetime"]);
    for(int i = 1 ; i <= max_ttl ; i++){
        clean_adj_list();
        update_forwarding_table();
        // clear the q_tr
        m_tr.lock();
        while(!q_tr.empty()) q_tr.pop();
        m_tr.unlock();
        struct timeval start_time;
        gettimeofday(&start_time, NULL);
        thread timer = thread(timer_thread, msg_lifetime);
        // if dest_id reachable, send PING msg
        if(adj_list.count(dest_id) != 0){
            shared_ptr<Message> ping = create_ping(to_string(sesid), i, dest_id);
            udt_send(ping);
        }

        // wait for resopnse or timeout
        shared_ptr<UDTMessage> msg = tr_wait_for_event();
        if(msg == nullptr){
            cout << i << " - *\n";
        } else if(msg->findType() == "TTLZERO" && msg->sesid == to_string(sesid)){
            stop_timer = true;
            struct timeval now;
            gettimeofday(&now, NULL);
            cout << i << " - " << msg->from << ", " << str_timestamp_diff_in_seconds(&start_time, &now) << "\n"; 
        } else if(msg->findType() == "PONG" && msg->sesid == to_string(sesid)){
            stop_timer = true;
            struct timeval now;
            gettimeofday(&now, NULL);
            // print message to console with RTT = now - start_time
            cout << i << " - " << msg->from << ", " << str_timestamp_diff_in_seconds(&start_time, &now) << "\n";
            cout << msg->from << " is reached in " << i << " steps\n";
            timer.join();
            return;
        }
        timer.join();
        sleep(3);
    }
    cout << "traceroute: " << dest_id << " not reached after " << max_ttl << " steps\n";
}

// ------------------------------- traceroute applicaton section above ----------------------------- //

/* End of functions for the client */
int main(int argc, char *argv[])
{
   process_options(argc, argv);
   if(argc == 2){
    //running as server
        // parse the configuration file & set up
        parseConfig(argv[1], configData);
        nodeID = configData["startup"]["host"] + ":" + configData["startup"]["port"];
        string a; string b;
        m.lock();
        GetObjID(nodeID, nullptr, a, b); //initialize the start time
        m.unlock();

        // set up log
        myFile.open(configData["startup"]["logfile"], ofstream::out|ofstream::trunc);
        mylog = &myFile;

        // print out pid to specified file
        ofstream pid = ofstream(configData["startup"]["pidfile"]);
        pid << (int)getpid();
        pid.close();

        master_socket_fd = create_master_socket(configData["startup"]["port"]);
        if (master_socket_fd != (-1)) {
            if (gnDebug) {
                string s = get_ip_and_port_for_server(master_socket_fd, 1);
                cout << "[SERVER]\tlistening at " << s << endl;
            }
            log_write(*mylog, "Server " + get_ip_and_port_for_server(master_socket_fd, 1) + " started");
            // start the threads
            thread console = thread(serverConsole);
            thread reaper = thread(reaperThread);
            thread neighbors = thread(neighborThread);
            for (;;) {
                int newsockfd = my_accept(master_socket_fd);
                if (newsockfd == (-1)) break;
                if(DEBUG) cout << "M LOCKED 712a\n";
                m.lock();
                if(DEBUG) cout << "M LOCKED 712b\n";
                if(master_socket_fd == -1){
                    shutdown(newsockfd, SHUT_RDWR);
                    close(newsockfd);
                    m.unlock();
                    break;
                } else {
                    shared_ptr<Connection> c = make_new_connection(newsockfd);
                    connections.push_back(c);
                    m.unlock();
                }
            }
            // join the threads
            console.join();
            reaper.join();
            neighbors.join();
            for(auto conn_ptr : connections){
                conn_ptr->read_thread_ptr->join();
            }
            log_write(*mylog, " Server " + get_ip_and_port_for_server(master_socket_fd, 0) + " stopped");
        }
    } else if(argc >= 6 && (string)argv[1] == "-c" && argc%2==0){
	//run as client
        string host = argv[2];
        string port = argv[3];
        int client_socket_fd = create_client_socket_and_connect(host, port);
        if (client_socket_fd == (-1)) {
            cerr << "Failed to connect to server at " << host << ":" << port << ".\n";
            exit(-1);
        } else {
            string client_ip_and_port = get_ip_and_port_for_client(client_socket_fd, 1);
            string server_ip_and_port = get_ip_and_port_for_client(client_socket_fd, 0);
            cout << "a2 client at " << client_ip_and_port << " connected to server at " << server_ip_and_port << "\n";
        }
        // for every uri and ofile
        for(int i = 4 ; i < argc ; i+=2){
            string uri = argv[i];
            string ofile = argv[i+1];
            send_HTTP_request(client_socket_fd, uri, ofile, host, port);
        }
        shutdown(client_socket_fd, SHUT_RDWR);
        close(client_socket_fd);
    }
    return 0;
}