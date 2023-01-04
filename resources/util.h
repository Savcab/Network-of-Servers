#ifndef _UTIL_H_
#define _UTIL_H_

#include <vector>
#include <string>
#include <map>
#include <fstream>

using namespace std;
/**
 * This function is to make splitting strings easier
 * WARNING: this function has a bug but fixing the bug would cause a lot of my code to crash becasue so much of my code already depend on it
 */
vector<string> split(string str, char delim);

/**
 * Split but without bug
 */
vector<string> better_split(string str, char delim);
/**
 * This function parses the config file and returns a vector of string
  Three different types of input
        lab5a sections INIFILE
        lab5a keys SECTIONNAME INIFILE
        lab5a value SECTIONNAME KEYNAME INIFILE
    outputs a vector<string> filled with the things that are asked
    It takes directly the argv and argc that are given from the command line
    It also takes an empty vector<string> that will be filled with data by the end
    Returns true if it successfully parses the config
    Returns false if there is an error
 */
bool parseConfig(string configPath, map<string, map<string, string>> &data);

/**
 * This function parses the header and returns a map<string, string> that represents the data in the header
 * It will also take an empty map<string, string> object and fill it with data from the header
 * filedir is the root file dir of the program
 * log is for logging the actions (REQUEST, and logging the entire request header) to the log file
 * ipPort is the ip and port of the client (used for logging)
 * NOTE: Will ALWAYS store the first line of the request under a ket called "FIRSTLINE"
 */
bool parseHeader(int client_socket_fd, map<string, string> &data, ostream &log);

/**
 * This function is for getting the size of a file
 * Will return -1 if the file doesn't exist or if there is an error
 * Will return an int with how many bytes the file is if it succeeds
 */
int get_file_size(string path);

/**
 * Helper function to round a double to a certain # of digits after the decimal point
 * returns the string format of that double
 */
string round(double val, int roundNum);

/**
 * Helper function
 * converts a int to string while simplifying it to KB, MD, or GB
 */
string bytes_to_string(double bytes);

/**
 * For trimmming whitespace off either end of a string
 */
string trim(string str);

/**
 * For trimming whitespaces and \r and \n off both ends of a string
 * */
string trimAll(string str);

#endif