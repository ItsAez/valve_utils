# valve_utils
c++17 key value and vpk parser

requires [fmt format](https://fmt.dev) library

**vpk.hpp** simple vpk parser

**kv.hpp** key value parser

**kv_utils.hpp** analyze blocks from kv files to write file with key occurrence data

**csgo.hpp** layer on top of kv.hpp for csgo items_game.txt parsing

example dumping all csgo paint kits (skins)
```c++
    std::filesystem::path csgo_folder = argv[ 1 ];
    std::filesystem::path items_game_path = csgo_folder / "scripts/items/items_game.txt";
    std::filesystem::path csgo_languages_path = csgo_folder / "resource/csgo_english.txt";
    
    auto items_game = csgo::items_game::from_file( items_game_path );
    auto csgo_english = csgo::language::from_file( csgo_languages_path );

    if ( !items_game || !csgo_english )
        return EXIT_FAILURE;

    for ( csgo::paint_kit_t pkit : items_game->paint_kits( ) )
    {
        fmt::print( "{: <5} {: <20}\n", pkit.id( ), pkit.name_token( &csgo_english.value( ) ) );
    }
```
```
1228  Temukau
1229  Sakkaku
1232  Banana Cannon
1233  Neoqueen
1234  Cyberforce
1235  Rebel
1236  Wild Child
...
```
