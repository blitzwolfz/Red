package redlang;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

import static redlang.TokenType.*;

class Scanner {
    private final String source;
    private final List<Token> tokens = new ArrayList<>();
    private int start = 0;
    private int current = 0;
    private int line = 1;
    private static final Map<String, TokenType> keywords;

    static {
        keywords = new HashMap<>();

        keywords.put("and",    AND);
        keywords.put("class",  CLASS);
        keywords.put("else",   ELSE);
        keywords.put("false",  FALSE);
        keywords.put("for",    FOR);
        keywords.put("fun",    FUN);
        keywords.put("if",     IF);
        keywords.put("nil",    NIL);
        keywords.put("or",     OR);
        keywords.put("print",  PRINT);
        keywords.put("return", RETURN);
        keywords.put("super",  SUPER);
        keywords.put("this",   THIS);
        keywords.put("true",   TRUE);
        keywords.put("var",    VAR);
        keywords.put("while",  WHILE);
    }

    Scanner(String source) {
        this.source = source;
    }

    List<Token> scanTokens() {
        while (!isAtEnd()) {
            this.start = current;
            scanToken();
        }

        tokens.add(new Token(EOF, "", null, this.line));
        return tokens;
    }

    private boolean isAtEnd() {
        return this.current >= this.source.length();
    }

    private void scanToken() {
        char c = advance();
        
        switch (c) {
            case '(': addToken(LEFT_PAREN); break;
            case ')': addToken(RIGHT_PAREN); break;
            case '{': addToken(LEFT_BRACE); break;
            case '}': addToken(RIGHT_BRACE); break;
            case ',': addToken(COMMA); break;
            case '.': addToken(DOT); break;
            case '-': addToken(MINUS); break;
            case '+': addToken(PLUS); break;
            case ';': addToken(SEMICOLON); break;
            case '*': addToken(STAR); break;
            case '!': addToken(match('=') ? BANG_EQUAL : BANG); break;
            case '=': addToken(match('=') ? EQUAL_EQUAL : EQUAL); break;
            case '<': addToken(match('=') ? LESS_EQUAL : LESS); break;
            case '>': addToken(match('=') ? GREATER_EQUAL : GREATER); break;

            case '/':
                if (match('/')) {
                    // A comment goes until the end of the line.
                    while (peek() != '\n' && !isAtEnd()) advance();
                }

                else {
                    addToken(SLASH);
                }
            break;
            
            // Ignore whitespace.
            case ' ': case '\r': case '\t': break;
            case '\n': line++; break;
            case '"': string(); break;

            default:
                if (this.isDigit(c)) this.number();
                else if (this.isAlpha(c)) this.identifier();
                else Red.error(line, "Unexpected character.");
                break;
        }
    }

    private char advance() {
        this.current++;
        return this.source.charAt(current - 1);
    }

    private boolean isDigit(char c) {
        return c >= '0' && c <= '9';
    }

    private void identifier() {
        while (this.isAlphaNumeric(peek())) advance();
        
        String text = source.substring(this.start, this.current);
        TokenType type = keywords.get(text);
        if (type == null) type = IDENTIFIER;
        this.addToken(type);
    }

    private boolean isAlpha(char c) {
        return (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
         c == '_';
    }

    private boolean isAlphaNumeric(char c) {
        return this.isAlpha(c) || this.isDigit(c);
    }

    private void number() {
        while (this.isDigit(peek())) this.advance();

        if (this.peek() == '.' && this.isDigit(this.peekNext())) {
            this.advance();

            while (this.isDigit(this.peek())) this.advance();
        }

        this.addToken(NUMBER,
        Double.parseDouble(this.source.substring(this.start, this.current)));
    }

    private char peekNext() {
        if (this.current + 1 >= this.source.length()) return '\0';
        return this.source.charAt(this.current + 1);
    }

    private void addToken(TokenType type) {
        addToken(type, null);
    }
    
    private void addToken(TokenType type, Object literal) {
        String text = this.source.substring(start, current);
        tokens.add(new Token(type, text, literal, this.line));
    }

    private boolean match (char ex) {
        if (isAtEnd() || source.charAt(this.current) != ex) return false;

        this.current++;
        return true;
    }

    private char peek() {
        if (isAtEnd()) return '\0';
        return source.charAt(current);
    }

    private void string() {
        while (peek() != '"' && !isAtEnd()) {
          if (peek() == '\n') line++;
          advance();
        }
    
        if (isAtEnd()) {
          Red.error(line, "Unterminated string.");
          return;
        }
    
        // The closing ".
        advance();
    
        // Trim the surrounding quotes.
        String value = source.substring(start + 1, current - 1);
        addToken(STRING, value);
      }
}
