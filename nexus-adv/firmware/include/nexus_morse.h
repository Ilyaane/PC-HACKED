#pragma once
#include <Arduino.h>

// International Morse subset for compact Cardputer messages.
namespace NexusMorse {
static constexpr const char* letters[]={
  ".-","-...","-.-.","-..",".","..-.","--.","....","..",".---",
  "-.-",".-..","--","-.","---",".--.","--.-",".-.","...","-",
  "..-","...-",".--","-..-","-.--","--..",
  "-----",".----","..---","...--","....-",".....","-....","--...",
  "---..","----."
};
static constexpr char punctuation[]=".,?!/-";
static constexpr const char* punctuationCodes[]={".-.-.-","--..--","..--..","-.-.--","-..-.","-....-"};
inline String encode(const String& text){
  String out;
  for(size_t i=0;i<text.length();i++){
    const char c=toupper((unsigned char)text[i]);String code;
    if(c==' '){if(out.length() && !out.endsWith("/"))out+="/ ";continue;}
    if(c>='A' && c<='Z')code=letters[c-'A'];
    else if(c>='0' && c<='9')code=letters[26+c-'0'];
    else{
      for(int j=0;j<6;j++)if(c==punctuation[j])code=punctuationCodes[j];
    }
    if(code.length()){out+=code;out+=' ';}
  }
  out.trim();return out;
}
inline char decodeSymbol(const String& code){
  for(int i=0;i<36;i++)if(code==letters[i])return i<26?'A'+i:'0'+i-26;
  for(int i=0;i<6;i++)if(code==punctuationCodes[i])return punctuation[i];
  return '?';
}
inline String decode(const String& morse){
  String word,output;
  for(size_t i=0;i<=morse.length();i++){
    const char c=i<morse.length()?morse[i]:' ';
    if(c=='.'||c=='-'){if(word.length()<10)word+=c;continue;}
    if(word.length()){output+=decodeSymbol(word);word="";}
    if(c=='/' && output.length() && !output.endsWith(" "))output+=' ';
  }
  return output;
}
} // namespace NexusMorse
