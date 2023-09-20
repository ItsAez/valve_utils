#pragma once

#include "csgo.hpp"

namespace valve::utils
{
    namespace fs = std::filesystem;

    class block_analyzer
    {
        struct block_data_t
        {
            using map_t = std::unordered_map<std::string_view, block_data_t>;

            block_data_t( valve::key_value::value_type type ) : m_type{ type } {};

            valve::key_value::value_type        m_type;
            map_t                               m_map;
            bool                                m_localized{ false };
            u32                                 m_count{ 0 };
            u32                                 m_min{ std::numeric_limits<u32>::max( ) };
            u32                                 m_max{ 0 };
        };

    public:
        void reset( )
        {
            m_root.m_map.clear( );
            m_root.m_count = 0;
        }

        void add_block( valve::key_value& kv, csgo::language* lang )
        {
            ++m_root.m_count;
            add_block_internal( m_root, kv, lang );
        }

        /* How the output works
        *
        *   {field name} ({0}/{1}) [{max}] or [{min..max}] #
        *
        *   {0} How many times this field appears in blocks analyzed
        *   {1} Block count
        *   {max} How many fields this block has
        *   {min}..{max} Range of minimum amount of times the field appears and maximum amount
        *   # This field is localization token
        *
        *   There is either max amount or range
        */
        void write( const fs::path& file )
        {
            std::ofstream out{ file, std::ios::binary };

            fmt::print( out, "{{\n" );
            write_block( out, m_root, 1 );
            fmt::print( out, "}}\n" );
        }
        void write( std::ostream& out )
        {
            fmt::print( out, "{{\n" );
            write_block( out, m_root, 1 );
            fmt::print( out, "}}\n" );
        }
    private:
        void add_block_internal( block_data_t& bd, valve::key_value& kv, csgo::language* lang )
        {
            auto& bd_map = bd.m_map;
            auto& kv_map = kv.map( );
            bd.m_min = std::min( bd.m_min, static_cast< u32 >( kv_map.size( ) ) );
            bd.m_max = std::max( bd.m_max, static_cast< u32 >( kv_map.size( ) ) );

            for ( auto& [k, v] : kv_map )
            {
                block_data_t* b = nullptr;
                if ( auto it = bd_map.find( k ); it != bd_map.end( ) )
                {
                    b = &it->second;
                }
                else
                {
                    bool is_value = v.type( ) == valve::key_value::value_type::VALUE;

                    b = &bd_map.try_emplace( k, block_data_t{ is_value ? valve::key_value::value_type::VALUE : valve::key_value::value_type::BLOCK } ).first->second;

                    if ( is_value && lang )
                        b->m_localized = !lang->get_token( v.value( ) ).empty( );
                }

                ++b->m_count;
                if ( v.type( ) == valve::key_value::value_type::BLOCK )
                    add_block_internal( *b, v, lang );
            }
        }

        void write_block( std::ostream& out, block_data_t& block, u32 depth )
        {
            std::string depth_pad( depth, '\t' );

            for ( auto& [k, b] : block.m_map )
            {
                fmt::print( out, "{}{}", depth_pad, k );

                if ( b.m_count != block.m_count )
                    fmt::print( out, " ({}/{})", b.m_count, block.m_count );

                if ( b.m_type == valve::key_value::value_type::BLOCK )
                {
                    if ( b.m_min == b.m_max )
                        fmt::print( out, " [{}]", b.m_max );
                    else
                        fmt::print( out, " [{}..{}]", b.m_min, b.m_max );

                    fmt::print( out, " {{\n" );

                    write_block( out, b, depth + 1 );

                    fmt::print( out, "{}}}\n", depth_pad );
                }
                else
                {
                    fmt::print( out, "{}\n", b.m_localized ? " #" : "" );
                }
            }
        }

    private:
        block_data_t m_root{ valve::key_value::value_type::BLOCK };
    };
}