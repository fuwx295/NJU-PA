/***************************************************************************************
* Copyright (c) 2014-2022 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>

/* We use the POSIX regex functions to process regular expressions.
 * Type 'man regex' for more information about POSIX regex functions.
 */
#include <regex.h>

#include "memory/vaddr.h"


enum {
  TK_NOTYPE = 256,

  /* TODO: Add more token types */
  TK_NUM, // 10, 16
  TK_REG,
  TK_VAR,

  TK_POS, TK_NEG, TK_DEREF,
  TK_EQ, TK_NEQ, TK_GT, TK_LT, TK_GE, TK_LE,
  TK_AND,
  TK_OR,


};

static struct rule {
  const char *regex;
  int token_type;
} rules[] = {

  /* TODO: Add more rules.
   * Pay attention to the precedence level of different rules.
   */

  {" +", TK_NOTYPE},    // spaces
  {"\\+", '+'},         // plus
  {"==", TK_EQ},        // equal
  {"-", '-'},
  {"\\(", '('},
  {"\\)", ')'},
  {"\\*", '*'},
  {"/", '/'},
  {"<", TK_LT},
  {"<=", TK_LE},
  {">", TK_GT},
  {">=", TK_GE},
  {"!=", TK_NEQ},
  {"&&", TK_AND},
  {"\\|\\|", TK_OR},

  {"[0-9]+", TK_NUM},
  {"\\$\\w+", TK_REG},
  {"[A-Za-z_]\\w*", TK_VAR},

};

// #define TYPE_FROM (type, types) type_from(type, types, ARRLEN(types))

static int bound_types[] = {')', TK_NUM, TK_REG};
static int nop_types[] = {'(', ')', TK_NUM, TK_REG};
static int op_types[] = {TK_NEG, TK_POS, TK_DEREF};


static bool type_from(int type, int types[], int len) {
  for (int i = 0; i < len; i++) {
    if (type == types[i]) return true;
  }
  return false;
}




#define NR_REGEX ARRLEN(rules)

static regex_t re[NR_REGEX] = {};

/* Rules are used for many times.
 * Therefore we compile them only once before any usage.
 */
void init_regex() {
  int i;
  char error_msg[128];
  int ret;

  for (i = 0; i < NR_REGEX; i ++) {
    ret = regcomp(&re[i], rules[i].regex, REG_EXTENDED);
    if (ret != 0) {
      regerror(ret, &re[i], error_msg, 128);
      panic("regex compilation failed: %s\n%s", error_msg, rules[i].regex);
    }
  }
}

typedef struct token {
  int type;
  char str[32];
} Token;

static Token tokens[320] __attribute__((used)) = {};
static int nr_token __attribute__((used))  = 0;

static bool make_token(char *e) {
  int position = 0;
  int i;
  regmatch_t pmatch;

  nr_token = 0;

  while (e[position] != '\0') {
    /* Try all rules one by one. */
    for (i = 0; i < NR_REGEX; i ++) {

      if (regexec(&re[i], e + position, 1, &pmatch, 0) == 0 && pmatch.rm_so == 0) {
        char *substr_start = e + position;
        int substr_len = pmatch.rm_eo;

        Log("match rules[%d] = \"%s\" at position %d with len %d: %.*s",
            i, rules[i].regex, position, substr_len, substr_len, substr_start);

        position += substr_len;

        /* TODO: Now a new token is recognized with rules[i]. Add codes
         * to record the token in the array `tokens'. For certain types
         * of tokens, some extra actions should be performed.
         */

        if (rules[i].token_type == TK_NOTYPE) break;
        // add to token
        tokens[nr_token].type = rules[i].token_type;

        switch (rules[i].token_type) {
          case TK_NUM:
          case TK_REG:
            strncpy(tokens[nr_token].str, substr_start, substr_len);
            tokens[nr_token].str[substr_len] = '\0';
            break;
          case '*':
          case '-':
          case '+':
            if (nr_token == 0 || !type_from(tokens[nr_token-1].type, bound_types, ARRLEN(bound_types))) {
              switch (rules[i].token_type) {
                case '-': tokens[nr_token].type = TK_NEG; break;
                case '+': tokens[nr_token].type = TK_POS; break;
                case '*': tokens[nr_token].type = TK_DEREF; break; 
              }
            }
            break;
            
          //default: TODO();
        }
        nr_token++;

        break;
      }
    }

    if (i == NR_REGEX) {
      printf("no match at position %d\n%s\n%*.s^\n", position, e, position, "");
      return false;
    }
  }

  return true;
}


bool check_parentheses(int p, int q) {

  if (tokens[p].type == '(' && tokens[q].type == ')') {
    int par = 0;
    for (int i = 0; i <= q; i++) {
      if (tokens[i].type == '(') par++;
      else if (tokens[i].type == ')') par--;

      if (par == 0) return i == q;
    }
  }
  return false;
}


int get_position(int p, int q) {
  int  ret = -1, par = 0, op_pre = 0;
  for (int i = p; i <= q; i++) {
    if (tokens[i].type == '(') {
      par++;
    } else if (tokens[i].type == ')') {
      if (par == 0) {
        return -1;
      }
      par--;
    } else if (type_from(tokens[i].type, nop_types, ARRLEN(nop_types))) {
      continue;
    } else if (par > 0) {
      continue;
    } else {
      int tmp_pre = 0;
      switch (tokens[i].type)
      {
      case TK_OR: tmp_pre++;
      case TK_AND: tmp_pre++;
      case TK_EQ: case TK_NEQ: tmp_pre++;
      case TK_LT: case TK_GT: case TK_GE: case TK_LE: tmp_pre++;
      case '+': case '-': tmp_pre++;
      case '*': case '/': tmp_pre++;
      case TK_NEG: case TK_DEREF: case TK_POS: tmp_pre++; break;
      default: return -1;
      }
      if (tmp_pre > op_pre || (tmp_pre == op_pre && !type_from(tokens[i].type, op_types, ARRLEN(op_types)))) {
        op_pre = tmp_pre;
        ret = i;
      }

    }
  }
  if (par != 0) return -1;
  return ret;
}

word_t eval_op(int i, bool *success) {
  switch (tokens[i].type) {
  case TK_NUM:
    if (strncmp("0x", tokens[i].str, 2) == 0) return strtol(tokens[i].str, NULL, 16);
    else return strtol(tokens[i].str, NULL, 10);
  case TK_REG:
    return isa_reg_str2val(tokens[i].str, success);
  default:
    *success = false;
    return 0;
  }
}

word_t calc1(int op, word_t val, bool *success) {
  switch (op) {
  case TK_NEG: return -val;
  case TK_POS: return val;
  case TK_DEREF: return vaddr_read(val, 8);
  default:
    *success = false;
  }
  return 0;

}

word_t calc2(int op, word_t val1, word_t val2, bool *success) {
  switch (op) {
    case '+': return val1 + val2;
    case '-': return val1 - val2;
    case '*': return val1 * val2;
    case '/': 
      if (val2 == 0) {
        *success = false;
        return 0;
      }
      return (sword_t)val1 / (sword_t) val2;
    case TK_AND: return val1 && val2;
    case TK_OR: return val1 || val2;
    case TK_EQ: return val1 == val2;
    case TK_NEG: return val1 != val2;
    case TK_LE: return val1 < val2;
    case TK_LT: return val1 <= val2;
    case TK_GT: return val1 > val2;
    case TK_GE: return val1 >= val2;
    default :
      *success = false;
      return 0;

  }
}



word_t eval(int p, int q, bool *success) {
  *success = true;
  if (p > q) {
    *success = false;
    return 0;
  } else if (p == q) {
    return eval_op(p, success);
  } else if (check_parentheses(p, q)) {
    return eval(p + 1, q -1, success);
  } else {
    int op = get_position(p, q);
    if (op < 0) {
      *success = false;
      return 0;
    }
    bool ok1, ok2;
    word_t val1 = eval(p, op - 1, &ok1);
    word_t val2 = eval(op + 1, q, &ok2);


    if (!ok2) {
      *success = false;
      return 0;
    }
    word_t ret;
    if (ok1) {
      ret = calc2(tokens[op].type, val1, val2, success);
    } else {
      ret = calc1(tokens[op].type,val2, success);
    }
    return ret;

  }
}



word_t expr(char *e, bool *success){
  if (!make_token(e)) {
    *success = false;
    return 0;
  }

  return eval(0, nr_token - 1, success);
  /* TODO: Insert codes to evaluate the expression. */
  //TODO();
}
