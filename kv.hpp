#pragma once

#include "types.hpp"

#include <string>
#include <string_view>
#include <charconv>
#include <variant>
#include <optional>
#include <unordered_map>
#include <stack>
#include <fstream>
#include <filesystem>

// define KV_PRINT_ERRORS for error printing
#include <fmt/format.h>
#include <fmt/ostream.h>

namespace valve
{
    namespace fs = std::filesystem;

#ifndef VALVE_UTIL_GUARD
#define VALVE_UTIL_GUARD
    // Branchless ascii tolower
    constexpr char tolower( char c )
    {
        return c + ( c >= 'A' && c <= 'Z' ) * 32;
    }
#endif // VALVE_UTIL_GUARD
    
    struct value_t
    {
        value_t( std::string_view value ) : m_value{ value } {}

        inline operator std::string_view( )
        {
            return m_value;
        }
        inline std::string as_str( ) const
        {
            return std::string{ m_value };
        }
        inline std::string_view as_str_v( ) const
        {
            return m_value;
        }
        // Supports strings that start with # or 0x
        inline std::optional<i32> as_hex_int( ) const
        {
            // Skip # or 0x start
            i32 skip = 0;
            if ( !m_value.empty( ) )
            {
                if ( m_value[ 0 ] == '#' )
                    skip = 1;
                else if ( m_value.size( ) > 1 && m_value[ 0 ] == '0' && tolower( m_value[ 1 ] ) == 'x' )
                    skip = 2;
            }

            i32 value = 0;
            auto result = std::from_chars( m_value.data( ) + skip, m_value.data( ) + m_value.size( ), value, 16 );

            if ( result.ec == std::errc::invalid_argument || result.ec == std::errc::result_out_of_range )
                return std::nullopt;

            return value;
        }
        inline std::optional<i32> as_int( ) const
        {
            i32 value = 0;
            auto result = std::from_chars( m_value.data( ), m_value.data( ) + m_value.size( ), value );

            if ( result.ec == std::errc::invalid_argument || result.ec == std::errc::result_out_of_range )
                return std::nullopt;

            return value;
        }
        inline std::optional<f32> as_float( ) const
        {
            f32 value = 0;
            auto result = std::from_chars( m_value.data( ), m_value.data( ) + m_value.size( ), value );
            
            if ( result.ec == std::errc::invalid_argument || result.ec == std::errc::result_out_of_range )
                return std::nullopt;
            
            return value;
        }

        std::string_view m_value;
    };

    class key_value
    {
        friend class kv_file;

        struct case_insensitive_hash
        {
            // FNV-1a 32bit hash
            std::size_t operator()( const std::string_view s ) const
            {
                size_t hash = 0x811c9dc5;

                for ( i32 i = 0; i < s.size( ); i++ )
                {
                    char c = s[ i ];
                    hash ^= tolower( c );
                    hash *= 0x01000193;
                }

                return hash;
            }
        };
        struct case_insensitive_equal
        {
            bool operator()( const std::string_view a, const std::string_view b ) const
            {
                if ( a.size( ) != b.size( ) )
                    return false;

                for ( i32 i = 0; i < a.size( ); i++ )
                {
                    if ( tolower( a[ i ] ) != tolower( b[ i ] ) )
                        return false;
                }

                return true;
            }
        };

    public:
        enum class value_type { VALUE, BLOCK };
        using kv_map_t = std::unordered_map<std::string_view, key_value, case_insensitive_hash, case_insensitive_equal>;
        using var_t = std::variant<std::string_view, kv_map_t>;

        // Construct value
        key_value( std::string_view key, std::string_view value ) :
            m_type{ value_type::VALUE },
            m_key{ key },
            m_var{ value }
        {}
        // Construct block
        key_value( std::string_view key ) :
            m_type{ value_type::BLOCK },
            m_key{ key },
            m_var{ kv_map_t{} }
        {}

        value_type type( )
        {
            return m_type;
        }
        value_t key( )
        {
            return value_t{ m_key };
        }
        value_t value( )
        {
            return value_t{ std::get<std::string_view>( m_var ) };
        }
        kv_map_t& map( )
        {
            return std::get<kv_map_t>( m_var );
        }

        key_value* find( std::string_view key )
        {
            if ( m_type == value_type::VALUE )
                return nullptr;

            auto& kv_map = map( );

            if ( auto result = kv_map.find( key ); result != kv_map.end( ) )
                return &result->second;

            return nullptr;
        }
        key_value* find_block( std::string_view key )
        {
            if ( key_value* kv = find( key ); kv && kv->m_type == value_type::BLOCK )
                return kv;

            return nullptr;
        }
        key_value* find_value( std::string_view key )
        {
            if ( key_value* kv = find( key ); kv && kv->m_type == value_type::VALUE )
                return kv;

            return nullptr;
        }
        key_value* find_recursive( std::string_view key )
        {
            std::stack<kv_map_t*> map_stack;
            map_stack.push( &map( ) );

            while ( !map_stack.empty( ) )
            {
                kv_map_t* map = map_stack.top( );
                map_stack.pop( );

                if ( auto it = map->find( key ); it != map->end( ) )
                    return &it->second;

                for ( auto& [k, v] : *map )
                {
                    if ( v.m_type != value_type::BLOCK )
                        continue;

                    map_stack.push( &v.map( ) );
                }
            }

            return nullptr;
        }

        // Unsafe
        key_value& operator[]( std::string_view key )
        {
            return *find( key );
        }

    private:
        value_type       m_type;
        std::string_view m_key;
        var_t            m_var;
    };

    class kv_file
    {
        struct parser_t
        {
            parser_t( const char* text ) : m_text{ text }, m_start{ text }, m_current{ text }, m_line{ 1 } {};

            const char* m_text;
            const char* m_start;
            const char* m_current;
            u32         m_line;

            inline void reset_start( )
            {
                m_start = m_current;
            }
            inline void advance( )
            {
                m_current++;
            }
            inline void skip_whitespace( )
            {
                char c = *m_current;
                while ( c == ' ' || c == '\r' || c == '\t' || c == '\n' )
                {
                    if ( c == '\n' )
                        ++m_line;
                    c = *++m_current;
                }
            }
            inline bool is_end( )
            {
                return *m_current == '\0';
            }
            inline char prev( )
            {
                return m_current[ -1 ];
            }
            inline char peek( )
            {
                return *m_current;
            }
            inline char peek_next( )
            {
                if ( is_end( ) ) return '\0';
                return m_current[ 1 ];
            }
            inline bool match( char c )
            {
                if ( is_end( ) ) return false;
                if ( *m_current != c ) return false;
                ++m_current;
                return true;
            }

            inline std::optional<std::string_view> string( )
            {
                // consume starting quote
                advance( );

                //while ( !( prev( ) != '\\' && peek( ) == '"' ) && peek( ) != '\n' && !is_end( ) )
                // valve kv file values span over to new lines so had to remove newline check
                while ( !( prev( ) != '\\' && peek( ) == '"' ) && !is_end( ) )
                    advance( );

                // Unterminated string
                if ( is_end( ) )
                {
                    fmt::print( "Unterminated string on line {}!\n", m_line );
                    return std::nullopt;
                }

                // consume closing quote
                if ( !match( '"' ) )
                {
                    fmt::print( "Unterminated string on line {}!\n", m_line );
                    return std::nullopt;
                }

                return std::string_view{ m_start + 1, static_cast< size_t >( m_current - m_start ) - 2 };
            }
            inline std::string_view current_line( )
            {
                const char* start = m_start;
                usize length = 0;
                
                // if new line not found start will be m_text - 1 and skip new line will fix it
                while ( start >= m_text && *m_current != '\n' )
                    --start;
                // skip new line
                ++start;

                for ( const char* ptr = start; *ptr != '\n' && *ptr != '\0'; ++ptr )
                    ++length;

                return std::string_view{ start, length };
            }
        };

    public:
        kv_file( ) = default;
        // To be able to steal memory from csgo::language string
        kv_file( std::string&& str ) : m_data{ std::move( str ) } {}

        static std::optional<kv_file> from_file( const fs::path& file )
        {
            kv_file kvf;

            if ( !kvf.load( file ) || !kvf.parse( ) )
                return std::nullopt;

            return kvf;
        }
        static std::optional<kv_file> from_string( std::string_view str )
        {
            kv_file kvf;
            kvf.load( str );

            if ( !kvf.parse( ) )
                return std::nullopt;

            return kvf;
        }

        // Load and parse the file
        bool load( const fs::path& file )
        {
            size_t size = fs::file_size( file );

            std::ifstream in( file, std::ios::binary );

            if ( !in.good( ) )
                return false;

            m_data.resize( size );
            in.read( m_data.data( ), size );
            return parse( );
        }
        // Load and parse the file
        bool load( std::string_view str )
        {
            size_t size = str.size( );
            m_data.resize( size );
            std::memcpy( m_data.data( ), str.data( ), str.size( ) );
            return parse( );
        }

        // Only call if you passed string in constructor load() calls parse already
        bool parse( )
        {
            m_parser = parser_t{ m_data.c_str( ) };
            m_root.map( ).clear( );

            std::stack<key_value*> scope;
            scope.push( &m_root );

#ifdef KV_PRINT_ERRORS
            auto print_error_line = [ this ]( ) -> void
            {
                auto curr_line = m_parser.current_line( );
                fmt::print( "{}\n{: >{}}\n", curr_line, '^', static_cast< usize >( m_parser.m_current - curr_line.data( ) ) );
            };
#endif // KV_PRINT_ERRORS

            while ( m_parser.skip_whitespace( ), m_parser.reset_start( ), !m_parser.is_end( ) )
            {
                char c = m_parser.peek( );

                // Value or Block
                if ( c == '"' )
                {
                    std::optional<std::string_view> key = m_parser.string( );

                    if ( !key )
                    {
#ifdef KV_PRINT_ERRORS
                        print_error_line( );
#endif // KV_PRINT_ERRORS
                        return false;
                    }

                    m_parser.skip_whitespace( );
                    m_parser.reset_start( );

                    // value
                    if ( m_parser.peek( ) == '"' )
                    {
                        std::optional<std::string_view> value = m_parser.string( );

                        if ( !value )
                        {
#ifdef KV_PRINT_ERRORS
                            print_error_line( );
#endif // KV_PRINT_ERRORS
                            return false;
                        }

                        // Conditionals not supported [$PS3], [$XBOX360], [$WIN32] so skip them
                        while ( m_parser.peek( ) != '\n' )
                            m_parser.advance( );

                        key_value* parent = scope.top( );

                        parent->map( ).try_emplace( *key, key_value{ *key, *value } );
                    }

                    // block
                    else if ( m_parser.peek( ) == '{' )
                    {
                        // consume {
                        m_parser.advance( );

                        key_value* parent = scope.top( );
                        key_value::kv_map_t& map = parent->map( );

                        // Find block if it already exists
                        key_value* block = nullptr;

                        if ( auto result = map.find( *key ); result != map.end( ) )
                            block = &result->second;
                        else
                            block = &parent->map( ).try_emplace( *key, key_value{ *key } ).first->second;

                        scope.push( block );
                    }

                    continue;
                }

                // Skip comments
                else if ( c == '/' && m_parser.peek_next( ) == '/' )
                {
                    while ( m_parser.peek( ) != '\n' && !m_parser.is_end( ) )
                        m_parser.advance( );
                    continue;
                }

                // Block end
                else if ( c == '}' )
                {
                    m_parser.advance( );
                    scope.pop( );
                    continue;
                }

                // Error
#ifdef KV_PRINT_ERRORS
                fmt::print( "Unexpected charachter\n" );
                print_error_line( );
#endif // KV_PRINT_ERRORS
                m_root.map( ).clear( );
                return false;
            }

            return true;
        }

        key_value& root( )
        {
            return m_root;
        }

        key_value* find( std::string_view key )
        {
            return m_root.find( key );
        }
        key_value* find_block( std::string_view key )
        {
            return m_root.find_block( key );
        }
        key_value* find_value( std::string_view key )
        {
            return m_root.find_value( key );
        }
        // Unsafe
        key_value& operator[]( std::string_view key )
        {
            return *find( key );
        }

        void write( const fs::path& path )
        {
            std::ofstream out( path, std::ios::binary );

            write_block( out, m_root, 0 );
        }

    private:
        void write_block( std::ofstream& out, key_value& block, u32 depth )
        {
            std::string depth_pad( depth, '\t' );

            for ( auto& [key, kv] : block.map( ) )
            {
                if ( kv.type( ) == key_value::value_type::VALUE )
                    fmt::print( out, "{}\"{}\" \"{}\"\n", depth_pad, key, kv.value( ).as_str_v( ) );
                else if ( kv.type( ) == key_value::value_type::BLOCK )
                {
                    fmt::print( out, "{0}\"{1}\"\n{0}{{\n", depth_pad, key );
                    write_block( out, kv, depth + 1 );
                    fmt::print( out, "{}}}\n", depth_pad );
                }
            }
        }

    private:
        parser_t         m_parser{ nullptr };
        key_value        m_root{ "root" };
        std::string      m_data;
    };
}