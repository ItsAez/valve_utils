#pragma once

#include "kv.hpp"

namespace csgo
{
    using namespace valve;

    // Text helper class
    class text_file
    {
    public:
        bool load( const fs::path& file )
        {
            std::ifstream in( file, std::ios::binary );

            if ( !in.good( ) )
                return false;

            m_buffer.resize( fs::file_size( file ) );
            in.read( ( char* )m_buffer.data( ), m_buffer.size( ) );
            return true;
        }
        void load( std::string_view str )
        {
            m_buffer.resize( str.size( ) );
            std::memcpy( m_buffer.data( ), str.data( ), str.size( ) );
        }

        u8* bytes( )
        {
            return reinterpret_cast< u8* >( m_buffer.data( ) );
        }
        usize size( )
        {
            return m_buffer.size( );
        }
        std::string& str( )
        {
            return m_buffer;
        }
        std::string_view as_str_v( )
        {
            return std::string_view{ m_buffer.data( ), m_buffer.size( ) };
        }

        // Reset to start
        void reset( )
        {
            m_file_ptr = 0;
        }
        // Reads line by line when end of file returns empty string
        std::string_view read_line( )
        {
            u32 start = m_file_ptr;
            while ( m_file_ptr < m_buffer.size( ) && m_buffer[ m_file_ptr ] != '\n' )
                ++m_file_ptr;

            if ( start == m_file_ptr )
                return std::string_view{};

            // dont include '\r\n' or '\n' in the line
            u32 skip_size = m_buffer[ m_file_ptr - 1 ] == '\r' ? 2 : 1;

            ++m_file_ptr; // skip '\n'
            return std::string_view{ reinterpret_cast< char* >( &m_buffer[ start ] ), ( m_file_ptr - start ) - skip_size };
        }

        bool utf16_le_bom( )
        {
            return m_buffer.size( ) >= 2 && *reinterpret_cast< u16* >( m_buffer.data( ) ) == 0xFEFF; // UTF-16 LE BOM
        }
        void convert_utf16_to_utf8( )
        {
            u32 skip_amount = utf16_le_bom( ) ? 2 : 0;
            m_buffer = convert_utf32_to_utf8( convert_utf16_to_utf32( std::u16string_view{ ( char16_t* )( m_buffer.data( ) + skip_amount ), ( m_buffer.size( ) - skip_amount ) / 2 } ) );
        }

    private:
        std::string	convert_utf32_to_utf8( std::u32string_view utf32 )
        {
            std::string utf8;
            utf8.reserve( utf32.size( ) );

            for ( uint32_t i = 0; i < utf32.size( ); i++ )
            {
                char32_t c = utf32[ i ];

                if ( c <= 0x7F )
                    utf8.push_back( ( char )( c ) );

                else if ( c <= 0x7FF )
                {
                    utf8.insert( utf8.end( ), {
                        ( char )( 0xC0 | ( ( c >> 6 ) & 0x1F ) ),
                        ( char )( 0x80 | ( c & 0x3F ) )
                    } );
                }

                else if ( c <= 0xFFFF )
                {
                    utf8.insert( utf8.end( ), {
                        ( char )( 0xE0 | ( ( c >> 12 ) & 0xF ) ),
                        ( char )( 0x80 | ( ( c >> 6 ) & 0x3F ) ),
                        ( char )( 0x80 | ( c & 0x3F ) )
                    } );
                }

                else if ( c <= 0x10FFFF )
                {
                    utf8.insert( utf8.end( ), {
                        ( char )( 0xF0 | ( ( c >> 18 ) & 0x7 ) ),
                        ( char )( 0x80 | ( ( c >> 12 ) & 0x3F ) ),
                        ( char )( 0x80 | ( ( c >> 6 ) & 0x3F ) ),
                        ( char )( 0x80 | ( c & 0x3F ) )
                        } );
                }
            }

            return utf8;
        }
        std::u32string convert_utf16_to_utf32( std::u16string_view utf16 )
        {
            std::u32string utf32;
            utf32.reserve( utf16.size( ) );

            char16_t high_surrogate = 0;
            for ( uint32_t i = 0; i < utf16.size( ); i++ )
            {
                if ( utf16[ i ] < 0xD800 || utf16[ i ] > 0xDFFF )
                {
                    utf32.push_back( utf16[ i ] );
                    continue;
                }

                if ( ( utf16[ i ] & 0xFC00 ) == 0xD800 )
                {
                    high_surrogate = utf16[ i ];
                    continue;
                }

                if ( high_surrogate )
                {
                    if ( ( utf16[ i ] & 0xFC00 ) == 0xDC00 )
                        utf32.push_back( ( ( high_surrogate & 0x3FF ) << 10 ) + ( utf16[ i ] & 0x3FF ) + 0x10000 );

                    high_surrogate = 0;
                }
            }

            return utf32;
        }

    private:
        u32             m_file_ptr{ 0 };
        std::string     m_buffer;
    };

    class language
    {
    public:
        static std::optional<language> from_file( const fs::path& file )
        {
            language lang;

            if ( !lang.load( file ) )
                return std::nullopt;

            return lang;
        }
        static std::optional<language> from_string( std::string_view str )
        {
            language lang;

            if ( !lang.load( str ) )
                return std::nullopt;

            return lang;
        }

        bool load( const fs::path& file )
        {
            return load_impl( file );
        }
        bool load( std::string_view str )
        {
            return load_impl( str );
        }

        bool is_empty( )
        {
            return m_tokens->map( ).empty( );
        }
        kv_file& kv( )
        {
            return m_kv_file;
        }

        std::string_view get_token( std::string_view key, language* fallback = nullptr )
        {
            if ( key.empty( ) )
                return std::string_view{};

            if ( key[ 0 ] == '#' )
                key = key.substr( 1 );

            key_value* result = m_tokens->find_value( key );

            if ( !result && fallback )
                return fallback->get_token( key );

            return result ? result->value( ).as_str_v( ) : std::string_view{};
        }

    private:
        // TODO: kinda ugly
        template <typename T>
        bool load_impl( T file_or_str )
        {
            text_file lang_txt;
            
            if constexpr ( std::is_same_v<T, std::filesystem::path> )
            {
                if ( !lang_txt.load( file_or_str ) )
                    return false;
            }
            else
            {
                lang_txt.load( file_or_str );
            }

            // CSGO Language files are in utf16
            if ( lang_txt.utf16_le_bom( ) )
                lang_txt.convert_utf16_to_utf8( );

            m_kv_file = kv_file{ std::move( lang_txt.str( ) ) };

            if ( !m_kv_file.parse( ) )
                return false;

            m_tokens = m_kv_file.find_block( "lang" );
            if ( !m_tokens )
                return false;

            m_tokens = m_tokens->find_block( "Tokens" );
            if ( !m_tokens )
                return false;

            return true;
        }

    private:
        kv_file          m_kv_file;
        key_value*       m_tokens{ nullptr };
    };

#pragma region MACROS
// Returns key's value as string
#define CSGO_STRING(name, key)                                              \
            std::string_view name()                                         \
            {                                                               \
                if ( key_value* result = m_block->find( #key ); result )    \
                    return result->value( ).as_str_v( );                    \
                                                                            \
                return std::string_view{};                                  \
            }
// Returns key's value as localized string
#define CSGO_LOCALIZED(name, key)                                                           \
            std::string_view name( language* lang, language* fallback_lang = nullptr )      \
            {                                                                               \
                key_value* result = nullptr;                                                \
                if ( result = m_block->find( #key ); !result )                              \
                    return std::string_view{};                                              \
                                                                                            \
                std::string_view token_result = lang->get_token( result->value( ) );        \
                                                                                            \
                if ( token_result.empty( ) && fallback_lang )                               \
                    return fallback_lang->get_token( result->value( ) );                    \
                                                                                            \
                return token_result;                                                        \
            }

// Create both string and localized value
#define CSGO_STR_LOC(name, key)     \
    CSGO_STRING(name, key)          \
    CSGO_LOCALIZED(name, key)

// Returns key's value converted to int
#define CSGO_INT(name, key)                                                 \
            i32 name()                                                      \
            {                                                               \
                if ( key_value* result = m_block->find( #key ); result )    \
                    return result->value( ).as_int( ).value();              \
                                                                            \
                return 0;                                                   \
            }
// Returns key's value converted to float
#define CSGO_FLOAT(name, key)                                               \
            f32 name()                                                      \
            {                                                               \
                if ( key_value* result = m_block->find( #key ); result )    \
                    return result->value( ).as_float( ).value();            \
                                                                            \
                return 0.f;                                                 \
            }
// Returns key's value converted to int optional
#define CSGO_INT_OPT(name, key)                                             \
            std::optional<i32> name()                                       \
            {                                                               \
                if ( key_value* result = m_block->find( #key ); result )    \
                    return result->value( ).as_int( );                      \
                                                                            \
                return std::nullopt;                                        \
            }
// Returns key's value converted to float optional
#define CSGO_FLOAT_OPT(name, key)                                           \
            std::optional<f32> name()                                       \
            {                                                               \
                if ( key_value* result = m_block->find( #key ); result )    \
                    return result->value( ).as_float( );                    \
                                                                            \
                return std::nullopt;                                        \
            }
#pragma endregion

    template<typename T = key_value*>
    struct block_t
    {
        key_value* m_block;

        std::optional<T> find( std::string_view key )
        {
            if ( key_value* result = m_block->find( key ); result )
                return T{ result };

            return std::nullopt;
        }

        struct iterator
        {
            T operator*( )
            {
                return T{ &m_iterator->second };
            }
            void operator++( ) // prefix
            {
                ++m_iterator;
            }
            bool operator==( iterator other )
            {
                return m_iterator == other.m_iterator;
            }
            bool operator!=( iterator other )
            {
                return !( m_iterator == other.m_iterator );
            }

            key_value::kv_map_t::iterator m_iterator;
        };
        iterator begin( )
        {
            return iterator{ m_block->map( ).begin( ) };
        }
        iterator end( )
        {
            return iterator{ m_block->map( ).end( ) };
        }
        size_t size( )
        {
            return m_block->map( ).size( );
        }

        operator bool( )
        {
            return m_block;
        }
    };
    struct value_t
    {
        key_value* m_value;
    };

    struct item_t : block_t<>
    {
        i32 id( )
        {
            return m_block->key( ).as_int( ).value_or( -1 );
        }

        CSGO_STRING( name, name )
        CSGO_LOCALIZED( name, item_name )
        CSGO_STRING( name_token, item_name )
        CSGO_STR_LOC( item_type_name, item_type_name )
        CSGO_STRING( rarity_id, item_rarity )
        CSGO_STRING( image_inventory, image_inventory )
        CSGO_STRING( model_player, model_player )
        CSGO_STRING( model_world, model_world )
        CSGO_STRING( model_dropped, model_dropped )
    };
    struct items_t : block_t<item_t> {};

    struct rarity_t : block_t<>
    {
        std::string_view name( )
        {
            return m_block->key( ).as_str_v( );
        }

        CSGO_INT( id, value )
        CSGO_LOCALIZED( name_loc, loc_key_weapon )
        CSGO_STRING( name_token, loc_key_weapon )
        CSGO_STRING( color_id, color )
    };
    struct rarities_t : block_t<rarity_t> {};

    struct color_t : block_t<>
    {
        std::string_view id( )
        {
            return m_block->key( );
        }

        CSGO_STRING( hex_color, hex_color )
    };
    struct colors_t : block_t<color_t> {};

    struct paint_kit_t : block_t<>
    {
        i32 id( )
        {
            return m_block->key( ).as_int( ).value_or( -1 );
        }

        CSGO_STRING( name, name )
        CSGO_STR_LOC( name_token, description_tag )
        CSGO_STR_LOC( description, description_string )
        CSGO_FLOAT_OPT( wear_remap_min, wear_remap_min )
        CSGO_FLOAT_OPT( wear_remap_max, wear_remap_max )
    };
    struct paint_kits_t : block_t<paint_kit_t> {};

    struct paint_kit_rarity_t : value_t
    {
        std::string_view id( )
        {
            return m_value->key( );
        }
        std::string_view rarity_id( )
        {
            return m_value->value( );
        }
    };
    struct paint_kit_rarities_t : block_t<paint_kit_rarity_t> {};

    struct alternate_icon_t : block_t<>
    {
        i32 id( )
        {
            return m_block->key( ).as_int( ).value_or( -1 );
        }

        CSGO_STRING( icon_path, icon_path )
    };
    struct alternate_icons_t : block_t<alternate_icon_t> {};

#undef CSGO_STRING
#undef CSGO_LOCALIZED
#undef CSGO_STR_LOC
#undef CSGO_INT
#undef CSGO_FLOAT
#undef CSGO_INT_OPT
#undef CSGO_FLOAT_OPT

    class items_game
    {
    public:
        static std::optional<items_game> from_file( const fs::path& file )
        {
            items_game ig;

            if ( !ig.load( file ) )
                return std::nullopt;

            return ig;
        }
        static std::optional<items_game> from_string( std::string_view str )
        {
            items_game ig;

            if ( !ig.load( str ) )
                return std::nullopt;

            return ig;
        }

        bool load( const fs::path& file )
        {
            if ( !m_kv_file.load( file ) )
                return false;

            m_block = m_kv_file.find_block( "items_game" );

            if ( !m_block )
                return false;

            flatten_item_prefabs( );
            return true;
        }
        bool load( std::string_view str )
        {
            if ( !m_kv_file.load( str ) )
                return false;

            m_block = m_kv_file.find_block( "items_game" );

            if ( !m_block )
                return false;

            flatten_item_prefabs( );
            return true;
        }

        bool is_empty( )
        {
            return m_block->map( ).empty( );
        }
        kv_file& kv( )
        {
            return m_kv_file;
        }

        items_t items( )
        {
            return items_t{ m_block->find( "items" ) };
        }
        rarities_t rarities( )
        {
            return rarities_t{ m_block->find( "rarities" ) };
        }
        colors_t colors( )
        {
            return colors_t{ m_block->find( "colors" ) };
        }
        paint_kits_t paint_kits( )
        {
            return paint_kits_t{ m_block->find( "paint_kits" ) };
        }
        paint_kit_rarities_t paint_kit_rarities( )
        {
            return paint_kit_rarities_t{ m_block->find( "paint_kits_rarity" ) };
        }
        alternate_icons_t alternate_icons( )
        {
            // TODO: maybe do something about this can dereference nullptr
            return alternate_icons_t{ m_block->find( "alternate_icons2" )->find( "weapon_icons" ) };
        }

    private:
        void flatten_item_prefabs( )
        {
            key_value temp_kv{ "temp_kv" };
            key_value* prefabs = m_block->find( "prefabs" );

            auto fix_prefab_value = []( std::string_view value ) -> std::string_view
            {
                // some values have "valve " in front of them for some reason idk
                if ( value.find( "valve " ) == 0 )
                    return value.substr( 6 );
                return value;
            };

            for ( item_t i : items( ) )
            {
                key_value* block = i.m_block;

                if ( key_value* prefab_value = block->find_value( "prefab" ); prefab_value )
                {
                    temp_kv.map( ).clear( );
                    temp_kv.map( ).insert( block->map( ).begin( ), block->map( ).end( ) );

                    for ( key_value* prefab_block = prefabs->find_block( fix_prefab_value( prefab_value->value( ) ) );
                        prefab_block;
                        prefab_block = prefabs->find_block( fix_prefab_value( prefab_block->find_value( "prefab" )->value( ) ) ) )
                    {
                        for ( auto& [k, v] : prefab_block->map( ) )
                        {
                            key_value* result = temp_kv.find_recursive( k );

                            if ( result && result->type( ) == key_value::value_type::BLOCK )
                            {
                                result->map( ).insert( v.map( ).begin( ), v.map( ).end( ) );
                                continue;
                            }

                            temp_kv.map( ).insert( std::make_pair( k, v ) );
                        }

                        if ( !prefab_block->find_value( "prefab" ) )
                            break;
                    }

                    block->map( ).swap( temp_kv.map( ) );
                }
            }
        }

    private:
        kv_file    m_kv_file;
        key_value* m_block{ nullptr };
    };
}