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
#include <math.h>

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
#include "my_readwrite.h"
#include "my_socket.h"
#include "util.h"
#include "my_timestamp.h"


using namespace std;
/**
 * This function is to make splitting strings easier
 * WARNING: This function has a bug, but so much of my code depends on it that I can't change it without causing errors
 */
vector<string> split(string str, char delim)
{
    vector<string> ans;
    int pos = 0;
    for(int i = 0 ; i < (int)str.length() ; i++){
        if(str[i] == delim || i == (int)str.length()-1){
            ans.push_back(str.substr(pos, i-pos));
            pos = i+1;
        }
    }
    return ans;
}

/**
 * The no bug version of split
 */
vector<string> better_split(string str, char delim){
    vector<string> ans;
    string temp = "";
    for(int i = 0 ; i < (int)str.length() ; i++){
        if(str[i] == delim){
            ans.push_back(temp);
            temp = "";
        } else {
            temp += str[i];
        }
        // if reach the end of the string
        if(i == (int)str.length()-1){
            ans.push_back(temp);
        }
    }
    return ans;
}

/**
 * Three different types of input
        lab5a sections INIFILE
        lab5a keys SECTIONNAME INIFILE
        lab5a value SECTIONNAME KEYNAME INIFILE
    outputs a vector<string> filled with the things that are asked
 */
bool parseConfig(string configPath, map<string, map<string, string>>& data){
    int fileFd = open(configPath.c_str(), O_RDONLY);
    // if INIFILE is not found
    if(fileFd == -1){
        cout << "No such file as: " << configPath << endl;
        return false;
    }
    string line;
    // reads the config file  and updates the data 
    //{section, {key, value}}
    string currSection = "";
    while(read_a_line(fileFd, line) > 0){
        // to accouunt for empty lines or comments
        if((line.size()==0 && line[0]=='\n') || line[0]==';'){ continue;}
        // detect the start of a section
        if(line[0] == '['){
            string sectionName = "";
            for(int i = 1 ; line[i] != ']' ; i++){
                sectionName.push_back(line[i]);
            }
            data[sectionName] = {};
            currSection = sectionName;
        } else {
            // add keys and values to sections
            if(currSection != ""){
                vector<string> kv = split(line, '=');
                if(kv.size() == 2){
                    data[currSection][kv[0]] = kv[1];
                } else {
                    data[currSection][kv[0]] = "";
                }
            }
        }
    }
    shutdown(fileFd, SHUT_RDWR);
    close(fileFd);
    return true;
}

/**
 * This function parses the header and returns a map<string, string> that represents the data in the header
 * This function should be called after already reading the first line of the header
 */
bool parseHeader(int client_socket_fd, map<string, string> &data, ostream &log){
    //reading the response header
    string line;
    for(;;){
        if(read_a_line(client_socket_fd, line) == -1){
            return false;
        }
        // log the line to logfile
        log << "\t" << line;
        log.flush();
        //if the line is the empty line at the end of header
        if(line.size() == 2 && (line[0] == '\r') && (line[1]=='\n')){
            break;
        }
        //for a "normal" line
        vector<string> key_value = better_split(line, ':');
        string key = key_value[0];
        //convert all key to lower since case insensitive
        // for(int i = 0 ; i < (int)key.length() ; i++){
        //     key[i] = tolower(key[i]);
        // }
        //concatonate all the remaining indexes
        string value = "";
        for(int i = 1 ; i < (int)key_value.size() ; i++){
            value += key_value[i];
            if(i < (int)key_value.size()-1){
                value += ":";
            }
        }
        //ignore first char since it will be whitespace
        data[key] = trimAll(value);
    }
    return true;
}

/**
 * Use this code to return the file size of path.
 *
 * You should be able to use this function as it.
 *
 * @param path - a file system path.
 * @return the file size of path, or (-1) if failure.
 */
int get_file_size(string path)
{
    struct stat stat_buf;
    if (stat(path.c_str(), &stat_buf) != 0) {
        return (-1);
    }
    return (int)(stat_buf.st_size);
}

/**
 * Helper function to round a double to a certain # of digits after the decimal point
 * returns the string format of that double
 */
string round(double val, int roundNum){
    int temp = val*pow(10, roundNum);
    return to_string(temp/(int)pow(10, roundNum)) + "." + to_string(temp%(int)pow(10, roundNum));
}

/**
 * Helper function
 * converts a int to string while simplifying it to KB, MD, or GB
 */
string bytes_to_string(double bytes){
    string postfix = " B";
    if(bytes > 1000000000){
        bytes /= 1000000000.0;
        postfix = " GB";
    } else if(bytes > 1000000){
        bytes /= 1000000.0;
        postfix = " MB";
    } else if(bytes > 1000){
        bytes /= 1000.0;
        postfix = " KB";
    }
    return round(bytes, 2) + postfix;
}

/**
 * For trimming whitespaces off of a string
 * */
string trim(string str){
    string ans = str;
    ans.erase(ans.find_last_not_of(' ')+1);
    ans.erase(0, ans.find_first_not_of(' '));
    return ans;
}

/**
 * For trimming whitespaces and \r and \n off both ends of a string
 * */
string trimAll(string str){
    string ans = str;
    while((int)ans.size() > 0 && (ans[0] == '\r' || ans[0] == '\n' || ans[0] == ' ' || ans[ans.size()-1] == '\r' || ans[ans.size()-1] == '\n' || ans[ans.size()-1] == ' ')){
        ans = trim(ans);
        ans.erase(ans.find_last_not_of('\r')+1);
        ans.erase(0, ans.find_first_not_of('\r'));
        ans.erase(ans.find_last_not_of('\n')+1);
        ans.erase(0, ans.find_first_not_of('\n'));
    }
    return ans;
}