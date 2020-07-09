#ifndef HTTP_RESPON_INFO_H
#define HTTP_RESPON_INFO_H


const char* ok_200_title = "OK";
const char* created_201_title = "CREATED";

const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";

const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";

const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";

const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

const char* doc_root = "/var/www/html";

const char* error_401_title = "Unauthorized";

const char* error_login_form = "Wrong username or password.\n";
const char* success_login_form = "User login Successfully.\n";

const char* error_signup_form = "Username already exist.\n";
const char* success_signup_form = "User signup successfully.\n";



#endif