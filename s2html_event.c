
#include <stdio.h>
#include <string.h>
#include "s2html_event.h"
#include <ctype.h>

#define SIZE_OF_SYMBOLS (sizeof(symbols))
#define SIZE_OF_OPERATORS (sizeof(operators))
#define WORD_BUFF_SIZE	100

/********** Internal states and event of parser **********/
typedef enum
{
	PSTATE_IDLE,
	PSTATE_PREPROCESSOR_DIRECTIVE,
	PSTATE_SUB_PREPROCESSOR_MAIN,
	PSTATE_SUB_PREPROCESSOR_RESERVE_KEYWORD,
	PSTATE_SUB_PREPROCESSOR_ASCII_CHAR,
	PSTATE_HEADER_FILE,
	PSTATE_RESERVE_KEYWORD,
	PSTATE_NUMERIC_CONSTANT,
	PSTATE_STRING,
	PSTATE_SINGLE_LINE_COMMENT,
	PSTATE_MULTI_LINE_COMMENT,
	PSTATE_ASCII_CHAR
}pstate_e;

/********** global variables **********/

/* parser state variable */
static pstate_e state = PSTATE_IDLE;

/* sub state is used only in preprocessor state */
static pstate_e state_sub = PSTATE_SUB_PREPROCESSOR_MAIN;

/* event variable to store event and related properties */
static pevent_t pevent_data;
static int event_data_idx=0;

static char word[WORD_BUFF_SIZE];
static int word_idx=0;


static char* res_kwords_data[] = {"const", "volatile", "extern", "auto", "register",
   						   "static", "signed", "unsigned", "short", "long", 
						   "double", "char", "int", "float", "struct", 
						   "union", "enum", "void", "typedef", ""
						  };

static char* res_kwords_non_data[] = {"goto", "return", "continue", "break", 
							   "if", "else", "for", "while", "do", 
							   "switch", "case", "default","sizeof", ""
							  };

static char operators[] = {'/', '+', '*', '-', '%', '=', '<', '>', '~', '&', ',', '!', '^', '|'};
static char symbols[] = {'(', ')', '{', '[', ':'};

/********** state handlers **********/
pevent_t * pstate_idle_handler(FILE *fd, int ch);
pevent_t * pstate_single_line_comment_handler(FILE *fd, int ch);
pevent_t * pstate_multi_line_comment_handler(FILE *fd, int ch);
pevent_t * pstate_numeric_constant_handler(FILE *fd, int ch);
pevent_t * pstate_string_handler(FILE *fd, int ch);
pevent_t * pstate_header_file_handler(FILE *fd, int ch);
pevent_t * pstate_ascii_char_handler(FILE *fd, int ch);
pevent_t * pstate_reserve_keyword_handler(FILE *fd, int ch);
pevent_t * pstate_preprocessor_directive_handler(FILE *fd, int ch);
pevent_t * pstate_sub_preprocessor_main_handler(FILE *fd, int ch);

/********** Utility functions **********/

/* function to check if given word is reserved key word */
static int is_reserved_keyword(char *word)
{
	//printf("----------------debuging---------------------------\n");
	//printf("------>%s--\n",word);
	int idx = 0;

	/* search for data type reserved keyword */
	while(*res_kwords_data[idx])
	{
		if(strcmp(res_kwords_data[idx++], word) == 0)
			return RES_KEYWORD_DATA;
	}

	idx = 0; // reset index
	/* search for non data type reserved key word */
	while(*res_kwords_non_data[idx])
	{
		if(strcmp(res_kwords_non_data[idx++], word) == 0)
			return RES_KEYWORD_NON_DATA;
	}

	return 0; // word did not match, return false
}

/* function to check symbols */
static int is_symbol(char c)
{
	int idx;
	for(idx = 0; idx < SIZE_OF_SYMBOLS; idx++)
	{
		if(symbols[idx] == c)
			return 1;
	}

	return 0;
}

/* function to check operator */
static int is_operator(char c)
{
	int idx;
	for(idx = 0; idx < SIZE_OF_OPERATORS; idx++)
	{
		if(operators[idx] == c)
			return 1;
	}

	return 0;
}
/* to set parser event */
static void set_parser_event(pstate_e s, pevent_e e)
{
	pevent_data.data[event_data_idx] = '\0';
	pevent_data.length = event_data_idx;
	event_data_idx = 0;
	state = s;
	pevent_data.type = e;
}
/************ Event functions **********/

/* This function parses the source file and generate 
 * event based on parsed characters and string
 */
pevent_t *get_parser_event(FILE *fd)
{
	int ch;
	pevent_t *evptr = NULL;
	/* Read char by char */
	while((ch = fgetc(fd)) != EOF)
	{
#ifdef DEBUG
	//	putchar(ch);
#endif
		switch(state)
		{
			case PSTATE_IDLE :
				if((evptr = pstate_idle_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_SINGLE_LINE_COMMENT :
				if((evptr = pstate_single_line_comment_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_MULTI_LINE_COMMENT :
				if((evptr = pstate_multi_line_comment_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_PREPROCESSOR_DIRECTIVE :
				if((evptr = pstate_preprocessor_directive_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_RESERVE_KEYWORD :
				if((evptr = pstate_reserve_keyword_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_NUMERIC_CONSTANT :
				if((evptr = pstate_numeric_constant_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_STRING :
				if((evptr = pstate_string_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_HEADER_FILE :
				if((evptr = pstate_header_file_handler(fd, ch)) != NULL)
					return evptr;
				break;
			case PSTATE_ASCII_CHAR :
				if((evptr = pstate_ascii_char_handler(fd, ch)) != NULL)
					return evptr;
				break;
			default : 
				printf("unknown state\n");
				state = PSTATE_IDLE;
				break;
		}
	}

	/* end of file is reached, move back to idle state and set EOF event */
	set_parser_event(PSTATE_IDLE, PEVENT_EOF);

	return &pevent_data; // return final event
}


/********** IDLE state Handler **********
 * Idle state handler identifies
 ****************************************/

pevent_t * pstate_idle_handler(FILE *fd, int ch)
{
	int pre_ch;
	switch(ch)
	{
		case '\'' : // begining of ASCII char 
		if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -1, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	detect ascii char
				{
#ifdef DEBUG	
					printf("Detecting ascii start : \n");
#endif
					state = PSTATE_ASCII_CHAR;
					
					pevent_data.data[event_data_idx++] = ch;
				}
			break;

		case '/' :
			pre_ch = ch;
			if((ch = fgetc(fd)) == '*') // multi line comment
			{
				if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -2L, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	multi line comment begin 
				{
#ifdef DEBUG	
					printf("Multi line comment Begin : /*\n");
#endif
					state = PSTATE_MULTI_LINE_COMMENT;
					pevent_data.data[event_data_idx++] = pre_ch;
					pevent_data.data[event_data_idx++] = ch;
				}
			}
			else if(ch == '/') // single line comment
			{
				if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -2L, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	single line comment begin
				{
#ifdef DEBUG	
					printf("Single line comment Begin : //\n");
#endif
					state = PSTATE_SINGLE_LINE_COMMENT;
					pevent_data.data[event_data_idx++] = pre_ch;
					pevent_data.data[event_data_idx++] = ch;
				}
			}
			else // it is regular exp
			{
				pevent_data.data[event_data_idx++] = pre_ch;
				pevent_data.data[event_data_idx++] = ch;
			}
			break;
		case '#' :
		if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -1L, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	detect header 
				{
#ifdef DEBUG	
					printf("header start : \n");
					//printf("%s\n",pevent_data.data);
#endif
					state = PSTATE_PREPROCESSOR_DIRECTIVE;
					
					pevent_data.data[event_data_idx++] = ch;
				}
			break;
		case '\"' :
				if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -1L, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	detect string
				{
#ifdef DEBUG	
					printf("String detection  begin : \n");
#endif

					state = PSTATE_STRING;
					
					pevent_data.data[event_data_idx++] = ch;
				}
			break;

		case '0' ... '9' : // detect numeric constant
			if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -1L, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	detect numeric constant
				{
#ifdef DEBUG	
					printf("numeric constant  begin : \n");
#endif
					state = PSTATE_NUMERIC_CONSTANT;
					
					pevent_data.data[event_data_idx++] = ch;
				}
			break;

		case 'a' ... 'z' : // could be reserved key word
			pre_ch=ch;
			if(event_data_idx) // we have regular exp in buffer first process that
				{
					fseek(fd, -1, SEEK_CUR); // unget chars
					set_parser_event(PSTATE_IDLE, PEVENT_REGULAR_EXP);
					return &pevent_data;
				}
				else //	key words
				{
#ifdef DEBUG	
					printf("Keyword  begain :\n");
#endif
					state = PSTATE_RESERVE_KEYWORD;
					pevent_data.data[event_data_idx++] = ch;
				}		
			break;
		default : // Assuming common text starts by default.
			pevent_data.data[event_data_idx++] = ch;
			break;
	}

	return NULL;
}
pevent_t * pstate_preprocessor_directive_handler(FILE *fd, int ch)
{
	int pre_ch;
	switch(ch)
	{
		case '\n' : /* header ends here */
#ifdef DEBUG	
			printf("\n header ends here \n");
#endif
			pevent_data.data[event_data_idx++] = ch;
			set_parser_event(PSTATE_IDLE, PEVENT_PREPROCESSOR_DIRECTIVE);
			return &pevent_data;
		default :          //storing the header file into array
			if(ch=='<')			//in html <=&lt					
			{
				pevent_data.data[event_data_idx++] = '&';
				pevent_data.data[event_data_idx++] = 'l';
				pevent_data.data[event_data_idx++] = 't';

			}
			else if(ch=='>')			//in html >=&gt	
			{
				pevent_data.data[event_data_idx++] = '&';  
				pevent_data.data[event_data_idx++] = 'g';  
				pevent_data.data[event_data_idx++] = 't'; 
			}
			else
			pevent_data.data[event_data_idx++] = ch;		//strong the header
			break;
	}
	return NULL;
}
// pevent_t * pstate_sub_preprocessor_main_handler(FILE *fd, int ch)
// {
// 	/* write a switch case here to detect several events here
// 	 * This state is similar to Idle state with slight difference
// 	 * in state transition.
// 	 * return event data at the end of event
// 	 * else return NULL
// 	 */
// }

 pevent_t * pstate_header_file_handler(FILE *fd, int ch)
 {
	/* write a switch case here to store header file name
	 * return event data at the end of event
	 * else return NULL
	 */	
}
pevent_t * pstate_reserve_keyword_handler(FILE *fd, int ch)
{
	/* write a switch case here to store words
	 * return event data at the end of event
	 * else return NULL
	 */
// 	int pre_ch;
// 	switch(ch)
// 	{
// 		case ' ' : /* checking for keywords */
// #ifdef DEBUG	
// 			printf("\n keyword End \n");
// #endif
// 			pre_ch = ch;
// 			fseek(fd, -1L, SEEK_CUR); // unget chars
// 			//pevent_data.data[event_data_idx++] = ch;
// 			pevent_data.data[event_data_idx] = '\0';
// 			if ( is_reserved_keyword(pevent_data.data) == RES_KEYWORD_DATA )		//checking for reserved keywords
// 			{
// 				//printf("----------------debugeddddddddddd\n");
// 				set_parser_event(PSTATE_IDLE, PEVENT_RESERVE_KEYWORD);
// 				pevent_data.property=RES_KEYWORD_DATA;
// 			}	
// 			else if( is_reserved_keyword(pevent_data.data) == RES_KEYWORD_NON_DATA )		//checking for reserved keywords
// 			{
// 				set_parser_event(PSTATE_IDLE, PEVENT_RESERVE_KEYWORD);
// 				pevent_data.property=RES_KEYWORD_NON_DATA;
// 			}
// 			else
// 			{
// 				set_parser_event(PSTATE_IDLE, PEVENT_RESERVE_KEYWORD);
// 				//printf("----------------debuging");
// 				pevent_data.property=1;			//for identifiers
// 			}
				
// 				return &pevent_data;
// 		default :  // collect single line comment chars
// 			pevent_data.data[event_data_idx++] = ch;
// 			break;
// 	}
// 	return NULL;
	//printf("%c\n",ch);
	pevent_data.data[event_data_idx++]=ch;
	while ( (ch = fgetc(fd)) != EOF )
	{
		if  ((isalpha(ch))||ch=='_')		//checking for keywords and identifiers
		{
			pevent_data.data[event_data_idx++]=ch;			//storing onto array
		}
		else
		{
			pevent_data.data[event_data_idx++]='\0';
			break;
		}
	} 
	int status;
	if ((status = is_reserved_keyword(pevent_data.data)) == RES_KEYWORD_DATA )	
	{
		set_parser_event(PSTATE_IDLE,PEVENT_RESERVE_KEYWORD);
		pevent_data.property=RES_KEYWORD_DATA;			//if reserved keyword found
		fseek(fd,-1,SEEK_CUR);						//unget charecter to the file
		return &pevent_data;
	}
	else if ( status == RES_KEYWORD_NON_DATA)
	{
		set_parser_event(PSTATE_IDLE,PEVENT_RESERVE_KEYWORD);
		pevent_data.property=RES_KEYWORD_NON_DATA;		//if reserved keyword non data  found
		fseek(fd,-1,SEEK_CUR);						//unget charecter to the file
		return &pevent_data;
	}
	else
	{
		set_parser_event(PSTATE_IDLE,PEVENT_REGULAR_EXP);		
		pevent_data.property=50;						//if identifier found means
		fseek(fd,-1,SEEK_CUR);		//unget charecter to the file
		return &pevent_data;
	}
}
pevent_t * pstate_numeric_constant_handler(FILE *fd, int ch)
{
	/* write a switch case here to store digits
	 * return event data at the end of event
	 * else return NULL
	 */
	switch(ch)
	{
		case ' ' : /* Numeric Constants */
#ifdef DEBUG	
			printf("\n  Numeric Constants End \n");
#endif
			pevent_data.data[event_data_idx++] = ch;
			set_parser_event(PSTATE_IDLE, PEVENT_NUMERIC_CONSTANT);
			return &pevent_data;
		default :  // collect Numeric Constants 
			pevent_data.data[event_data_idx++] = ch;
			break;
	}
	return NULL;
}
pevent_t * pstate_string_handler(FILE *fd, int ch)
{
	/* write a switch case here to store string
	 * return event data at the end of event
	 * else return NULL
	 */
	int pre_ch;
	switch(ch)
	{
		case '\"' : /* string detecting */
#ifdef DEBUG	
			printf("\nstring  end\n");
#endif
			pre_ch = ch;
			pevent_data.data[event_data_idx++] = ch;
			set_parser_event(PSTATE_IDLE, PEVENT_STRING);
			return &pevent_data;
		default :  // collect string
			pevent_data.data[event_data_idx++] = ch;
			break;
	}

	return NULL;
}

pevent_t * pstate_single_line_comment_handler(FILE *fd, int ch)
{
	int pre_ch;
	switch(ch)
	{
		case '\n' : /* single line comment ends here */
#ifdef DEBUG	
			printf("\nSingle line comment end\n");
#endif
			pre_ch = ch;
			pevent_data.data[event_data_idx++] = ch;
			set_parser_event(PSTATE_IDLE, PEVENT_SINGLE_LINE_COMMENT);
			return &pevent_data;
		default :  // collect single line comment chars
			pevent_data.data[event_data_idx++] = ch;
			break;
	}

	return NULL;
}
pevent_t * pstate_multi_line_comment_handler(FILE *fd, int ch)
{
	int pre_ch;
	switch(ch)
	{
		case '*' : /* comment might end here */
			pre_ch = ch;
			pevent_data.data[event_data_idx++] = ch;
			if((ch = fgetc(fd)) == '/')
			{
#ifdef DEBUG	
				printf("\nMulti line comment End : */\n");
#endif
				pre_ch = ch;
				pevent_data.data[event_data_idx++] = ch;
				set_parser_event(PSTATE_IDLE, PEVENT_MULTI_LINE_COMMENT);
				return &pevent_data;
			}
			else // multi line comment string still continued
			{
				pevent_data.data[event_data_idx++] = ch;
			}
			break;
		case '/' :
			/* go back by two steps and read previous char */
			fseek(fd, -2L, SEEK_CUR); // move two steps back
			pre_ch = fgetc(fd); // read a char
			fgetc(fd); // to come back to current offset

			pevent_data.data[event_data_idx++] = ch;
			if(pre_ch == '*')
			{
				set_parser_event(PSTATE_IDLE, PEVENT_MULTI_LINE_COMMENT);
				return &pevent_data;
			}
			break;
		default :  // collect multi-line comment chars
			pevent_data.data[event_data_idx++] = ch;
			break;
	}

	return NULL;
}
pevent_t * pstate_ascii_char_handler(FILE *fd, int ch)
{
	/* write a switch case here to store ASCII chars
	 * return event data at the end of event
	 * else return NULL
	 */
	int pre_ch;
	switch(ch)
	{
		case '\'' : /* ascii charecter */
#ifdef DEBUG	
			printf("\nascii  end\n");
#endif
			pre_ch = ch;
			pevent_data.data[event_data_idx++] = ch;
			set_parser_event(PSTATE_IDLE, PEVENT_ASCII_CHAR);
			return &pevent_data;
		default :  // collect single line comment chars
			pevent_data.data[event_data_idx++] = ch;
			break;
	}

	return NULL;
}
/**** End of file ****/
