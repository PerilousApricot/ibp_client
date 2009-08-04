#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "iniparse.h"
#include "string_token.h"
#include "log.h"

#define BUFMAX 8192
typedef struct {  //** Used for Reading the ini file
   FILE *fd;
   char buffer[BUFMAX];
   int used;
} bfile_t;

//***********************************************************************
// _get_line - Reads a line of text from the file
//***********************************************************************

char * _get_line(bfile_t *bfd)
{
   char *comment;
   if (bfd->used == 1) return(bfd->buffer);  

   comment = fgets(bfd->buffer, BUFMAX, bfd->fd);
log_printf(15, "_get_line: fgets=%s", comment);

   if (comment == NULL) return(NULL);   //** EOF or Error

   //** Remove any comments
   comment = strchr(bfd->buffer, '#');
   if (comment != NULL) comment[0] = '\0';

log_printf(15, "_get_line: buffer=%s\n", bfd->buffer);
   bfd->used = 1;
   return(bfd->buffer);
}

//***********************************************************************
//  _parse_ele - Parses the element
//***********************************************************************

inip_element_t *_parse_ele(bfile_t *bfd)
{
  char *text, *key, *val, *last, *isgroup;
  int fin;
  inip_element_t *ele;

  while ((text = _get_line(bfd)) != NULL) {
     isgroup = strchr(text, '[');  //** Start of a new group
     if (isgroup != NULL) {
        log_printf(15, "_parse_ele: New group! text=%s\n", text);
        return(NULL);
     }

     bfd->used = 0;

     key = string_token(text, " =:\r\n", &last, &fin);
     log_printf(15, "_parse_ele: key=!%s!\n", key);
     if (fin == 0) {
        val = string_token(NULL, " =:\r\n", &last, &fin);

        ele = (inip_element_t *)malloc(sizeof(inip_element_t));
        assert(ele != NULL);

        ele->key = strdup(key);
        ele->value = (val == NULL) ? NULL : strdup(val);
        ele->next = NULL;

        log_printf(15, "_parse_ele: key=%s value=%s\n", ele->key, ele->value);
        return(ele);
     }
  }

  log_printf(15, "_parse_ele: END text=%s\n", text);

  return(NULL);
    
}

//***********************************************************************
//  _parse_group - Parses the group
//***********************************************************************

void _parse_group(bfile_t *bfd, inip_group_t *group)
{
    inip_element_t *ele, *prev;

    ele = _parse_ele(bfd);
    prev = ele;
    group->list = ele;
    ele = _parse_ele(bfd);
    while (ele != NULL) {
        prev->next = ele;
        prev = ele;
        ele = _parse_ele(bfd);
    }
}

//***********************************************************************
//  _next_group - Retreives the next group from the input file
//***********************************************************************

inip_group_t *_next_group(bfile_t *bfd)
{
  char *text, *start, *end, *last;
  int i;
  inip_group_t *g;

  while ((text = _get_line(bfd)) != NULL) {
     bfd->used = 0;

     start = strchr(text, '[');
     if (start != NULL) {   //** Got a match!
        end = strchr(start, ']');
        if (end == NULL) {
           printf("_next_group:  missing ] for group heading.  Parsing line: %s\n", text);
           abort();
        }

        end[0] = '\0';  //** Terminate the ending point
        start++;  //** Move the starting point to the next character
        text = string_token(start, " ", &last, &i);
        g = (inip_group_t *)malloc(sizeof(inip_group_t));
        assert(g != 0);
        g->group = strdup(text);
        g->list = NULL;
        g->next = NULL;
        log_printf(15, "_next_group: group=%s\n", g->group);
        _parse_group(bfd, g);
        return(g);
     }
  }

  return(NULL);
}

//***********************************************************************
//  _free_element - Frees an individual key/valure pair
//***********************************************************************

void _free_element(inip_element_t *ele)
{
  free(ele->key);
  free(ele->value);
  free(ele);
}

//***********************************************************************
//  _free_list - Frees memory associated with the list
//***********************************************************************

void _free_list(inip_element_t *ele)
{
  inip_element_t *next;

  while (ele != NULL) {
     next = ele->next;
     log_printf(15, "_free_list:   key=%s value=%s\n", ele->key, ele->value);
     _free_element(ele);
     ele = next;
  }

}


//***********************************************************************
//  _free_group - Frees the memory associated with a group structure
//***********************************************************************

void _free_group(inip_group_t *group)
{
   log_printf(15, "_free_group: group=%s\n", group->group);
   _free_list(group->list);
   free(group->group);
   free(group);
}

//***********************************************************************
// inip_destroy - Destroys an inip structure
//***********************************************************************

void inip_destroy(inip_file_t *inip)
{
   inip_group_t *group, *next;
   if (inip == NULL) return;
   
   group = inip->tree;
   while (group != NULL) {
       next = group->next;
       _free_group(group);
       group = next;
   }

   return;
}

//***********************************************************************
//  _find_key - Scans the group for the given key
//***********************************************************************

inip_element_t *_find_key(inip_group_t *group, const char *name)
{
  inip_element_t *ele;

  if (group == NULL) return(NULL);

  for (ele = group->list; ele != NULL; ele = ele->next) {
     if (strcmp(ele->key, name) == 0) return(ele);
  }

  return(NULL);
}


//***********************************************************************
//  _find_group - Scans the tree for the given group
//***********************************************************************

inip_group_t *_find_group(inip_file_t *inip, const char *name)
{
  inip_group_t *group;

  for (group = inip->tree; group != NULL; group = group->next) {
     if (strcmp(group->group, name) == 0) return(group);
  }

  return(NULL);
}

//***********************************************************************
//  _find_group_key - Returns the element pointer for the given group/key
//       combination
//***********************************************************************

inip_element_t *_find_group_key(inip_file_t *inip, const char *group_name, const char *key)
{
  log_printf(15, "_find_group_key: Looking for group=%s key=%s!\n", group_name, key);

  inip_group_t *g = _find_group(inip, group_name);
  if (g == NULL) {
     log_printf(15, "_find_group_key: group=%s key=%s Can't find group!\n", group_name, key);
     return(NULL);
  }

  log_printf(15, "_find_group_key: Found group=%s\n", group_name);

  inip_element_t *ele = _find_key(g, key);
  
  if (ele != NULL) {
     log_printf(15, "_find_group_key: group=%s key=%s value=%s\n", group_name, key, ele->value);
  }

  return(ele);
}

//***********************************************************************
// inip_get_* - Routines for getting group/key values
//***********************************************************************

int inip_get_integer(inip_file_t *inip, const char *group, const char *key, int def)
{
  inip_element_t *ele = _find_group_key(inip, group, key);
  if (ele == NULL) return(def);

  int n;
  sscanf(ele->value, "%d", &n);
  return(n);
}

//***********************************************************************

char *inip_get_string(inip_file_t *inip, const char *group, const char *key, char *def)
{
  char *value = def;
  inip_element_t *ele = _find_group_key(inip, group, key);
  if (ele != NULL) value = ele->value;

  if (value == NULL) return(NULL);

  return(strdup(value));
}


//***********************************************************************
//  inip_read - Reads a .ini file
//***********************************************************************

inip_file_t *inip_read(const char *fname)
{
  inip_file_t *inip;
  inip_group_t *group, *prev;
  bfile_t bfd;

  log_printf(15, "inip_read: Parsing file %s\n", fname);

  bfd.fd = fopen(fname, "r");
  if (bfd.fd == NULL) return(NULL);
  bfd.used = -1;
  
  inip = (inip_file_t *)malloc(sizeof(inip_file_t));
  assert(inip != NULL);

  group = _next_group(&bfd);
  inip->tree = NULL;
  prev = NULL;
  while (group != NULL) {
      if (_find_group(inip, group->group) != NULL) {
         _free_group(group);
      } else {
        if (inip->tree == NULL) inip->tree = group;
        if (prev != NULL) { prev->next = group; }         
        prev = group;         
      }

      group = _next_group(&bfd);
  }

  fclose(bfd.fd);

  return(inip);
}

