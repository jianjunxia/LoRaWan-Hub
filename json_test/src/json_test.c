#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "parson.h" //解析json的头文件

#define  uint32_t   unsigned int

//宏定义，调试代码使用
#define _DEBUG_CONF_

#ifdef  _DEBUG_CONF_
    #define     DEBUG_CONF(fmt,args...) fprintf(stderr,"[%d]: "fmt,__LINE__,##args);
#else   
    #define DEBUG_CONF(fmt,args)
#endif

//读json文件
void read_json(void);
//写json文件
void write_json(void);

int main(int argc, char const *argv[])
{
    /* code */
   read_json();
   write_json();
   read_json();
    return 0;
}

//读JSON文件
void read_json(void)
{
    char *read_cfg_path = "server_conf.json";
    char conf_obj[]     = "Server_conf";
    const char  *test   =  malloc(100); 
    char  *ip_name      = "server_ip"; 
    char  *ip_port      = "server_port";    
    uint32_t port;
 
    JSON_Value  *read_val;
    JSON_Object *read       = NULL;
    JSON_Object *read_conf  = NULL;

    if(access(read_cfg_path,R_OK) == 0)
    {
        DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);

        read_val = json_parse_file_with_comments(read_cfg_path);
        read     = json_value_get_object(read_val);
        if(read == NULL)
        {
            DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
            exit(EXIT_FAILURE);
        }
        read_conf = json_object_get_object(read,conf_obj);
        if(read_conf == NULL)
        {
            DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
        }
        test    = json_object_get_string(read_conf,ip_name);
        DEBUG_CONF("test ip address: %s\n",test);
        port = json_object_dotget_number(read_conf,ip_port);  
        DEBUG_CONF("test ip port: %d\n",port);
    }
    //每次操作JSON文件后需序列化文件，并释放指针
    json_serialize_to_file(read_val,"server_conf.json"); 
    json_value_free(read_val);

}
//写JSON文件
void write_json(void)
{
    char *read_cfg_path = "server_conf.json";
    char conf_obj[]     = "Server_conf";
    const char  *test   =  malloc(100); 
    char  *ip_name      = "server_ip"; 
    char  *ip_port      = "server_port";       
    char  *ip  = "152.123.146.100";
    uint32_t port = 1234;
 
    JSON_Value  *read_val;
    JSON_Object *read       = NULL;
    JSON_Object *read_conf  = NULL;

    if(access(read_cfg_path,R_OK) == 0)
    {
        DEBUG_CONF(" %s file is exit, parsing this file\n",read_cfg_path);

        read_val = json_parse_file_with_comments(read_cfg_path);
        read     = json_value_get_object(read_val);
        if(read == NULL)
        {
            DEBUG_CONF("ERROR: %s is not a valid JSON file\n",read_cfg_path);
            exit(EXIT_FAILURE);
        }
        read_conf = json_object_get_object(read,conf_obj);
        if(read_conf == NULL)
        {
            DEBUG_CONF("INFO: %s does not contain a JSON object named %s\n", read_cfg_path, conf_obj);
        }
        json_object_dotset_string(read_conf,ip_name,ip);
        //DEBUG_CONF("test ip address: %s\n",test);
        json_object_dotset_number(read_conf,ip_port,port);  
        //DEBUG_CONF("test ip port: %d\n",port);
    }
    //每次操作JSON文件后需序列化文件，并释放指针
    json_serialize_to_file(read_val,"server_conf.json"); 
    json_value_free(read_val);
}