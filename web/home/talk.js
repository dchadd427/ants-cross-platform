// The chat's two small helpers (docs/HOME_SCREEN.md, "Emoji and voice"): an emoji button with a picker, and a microphone button that types what the person says into the chat field. They stand beside Send. ES5, no module
// system: a page gets AntsHomeTalk, node gets module.exports (tests/scripts/web_home_talk_check.js). web/home/home.js loads this file and talk.css after the chat dock, once, and mounts it into the dock's own form; a page
// whose dock never comes has neither button, and what goes wrong in here stays here.
//   * Emoji: every single code point emoji of Unicode 9 or older that draws as a picture by itself, in eight groups, with a name and a few English search words each; own code and no library. Each one is a single code point
//     (a character that the hub's text rules keep as it is: no joiner, no tag character, no variation selector; tests/scripts/test_web_home_talk.py runs every one through hub/text.py). The picker has a tab for the
//     recently used ones and one for each group, a search field (the words of the names and the search words), the recently used ones above each group, a name line, and the keyboard (arrows, Home, End, Page keys,
//     Enter in the search field picks the first result). A pick goes in at the caret of the field, and only while the line stays within the hub's 200 code points (the field's own maxlength counts UTF-16 units, 400,
//     and an emoji outside the basic plane is two of them: so the count here is in code points, and 200 of them are never more than 400 units). The only thing kept is the list of the last 16 picks, in this browser (localStorage).
//   * Voice: the browser's own speech recognition (SpeechRecognition, or webkitSpeechRecognition), only on a secure page; a browser without it (Firefox) has no microphone button at all. It never sends: the words go into
//     the field, the interim ones too (replaced by the final ones), and the person presses Send. In Chrome and Edge the browser sends the audio to its maker's speech service; this page never sees audio, records nothing and
//     uploads nothing (no MediaRecorder, no getUserMedia), and keeps nothing: the button says so (title and label), and so does web/legal.html. It stops on a second press, on Escape, when the person types or sends, and
//     when the page is hidden. The language is the page's (navigator.language).
//   * Everything that the speech service returns is text from outside: it goes into the field as a value, never as markup.
(function (root, factory) {
    var api = factory();
    if (typeof module === 'object' && module.exports) module.exports = api;
    else root.AntsHomeTalk = api;
})(typeof self !== 'undefined' ? self : this, function () {
    'use strict';

    var ROW = 44;                                              // px: the box's one row (chat.js has the same)
    var BUBBLE = 6000;                                         // ms that the line of words stays
    var LIMIT = 200;                                           // code points of a chat line (the hub's own, and the dock's)
    var COLS = 8;                                              // emoji in a row, where the page cannot be asked how many there are (a fake page)
    var RECENT = 16, SHOWN = 160, KEY = 'ants.emoji.recent';   // the picks that are kept, the search results that are drawn, and where the picks are kept (this browser only)
    var TEXT = {
        emoji: 'Emoji', emojiList: 'Emoji: choose one to add it to your message', find: 'Search emoji (cat, heart, ant...)', findLabel: 'Search emoji', clear: 'Clear search', groups: 'Emoji groups', recent: 'Recently used',
        pickOne: 'Pick an emoji', noRecent: 'Emoji that you pick show up here.', none: 'No emoji found. Try another word.',
        voice: 'Voice to text: your browser\'s speech service hears you',
        asking: 'Your browser\'s speech service hears you. Allow the microphone if it asks.', listening: 'Listening. Press the microphone again to stop. Then press Send.',
        full: 'The message is full.', nothing: 'No speech was heard. Press the microphone and speak.', blocked: 'The microphone is blocked for this site. Allow it in your browser\'s site settings, then try again.', service: 'The browser\'s speech service is not available.', policy: 'This page does not allow the microphone.', mic: 'No microphone was found.',
        network: 'The speech service could not be reached. Check your connection and try again.', failed: 'Voice to text did not work.'
    };
    var ERR = { 'not-allowed': 'blocked', 'service-not-allowed': 'service', 'no-speech': 'nothing', 'audio-capture': 'mic', 'network': 'network' };     // (what the browser says went wrong -> a line of TEXT; 'aborted' is the page's own stop)
    // The emoji, by group: [id, name, entries separated by ';']. An entry is a name, then ~ and its search words (separated by ','), and stands for the next code point; '@1F600' sets the code point (in hex) of the entry after it.
    // Every one is a single code point that draws as a picture by itself (Emoji_Presentation, Unicode 9 or older: the hub keeps it as it is, and every Python of the hub knows it); left out on purpose: the regional indicator letters
    // (a flag needs two), the skin tone swatches, and everything that needs a joiner or a variation selector (the hub removes the joiner and keeps the selector as a second code point).
    var GROUPS = [
        ['smileys', 'Smileys',
            '@1F600;Grinning face~happy,smile,grin;Beaming face~happy,grin,teeth;Face with tears of joy~lol,laugh,funny;Grinning face with big eyes~happy,smile,yay;' +
            'Grinning face with smiling eyes~happy,smile,laugh;Grinning face with sweat~phew,nervous,relief;Grinning squinting face~laugh,lol,haha;Smiling face with halo~angel,innocent,blessed;' +
            'Smiling face with horns~devil,evil,naughty;Winking face~wink,flirt;Smiling face with smiling eyes~blush,happy,shy;Face savoring food~yum,tasty,delicious;' +
            'Relieved face~calm,relaxed,content;Smiling face with heart-eyes~love,crush,adore;Smiling face with sunglasses~cool,shades,swag;Smirking face~smug,sly,smirk;' +
            'Neutral face~meh,blank,straight;Expressionless face~blank,meh,unimpressed;Unamused face~bored,annoyed,unimpressed;Downcast face with sweat~tired,nervous;' +
            'Pensive face~sad,sorry,thoughtful;Confused face~unsure,puzzled;Confounded face~upset,frustrated;Kissing face~kiss,love;Face blowing a kiss~love,mwah,blow;' +
            'Kissing face with smiling eyes~kiss,love;Kissing face with closed eyes~kiss,love;Face with tongue~silly,playful;Winking face with tongue~silly,joke,crazy;' +
            'Squinting face with tongue~silly,crazy;Disappointed face~sad,unhappy,down;Worried face~worry,anxious,nervous;Angry face~mad,upset,furious;Pouting face~rage,mad,angry;' +
            'Crying face~cry,sad,tear;Persevering face~struggle,persevere,effort;Face with steam from nose~proud,triumph,huffing;Sad but relieved face~relief,sweat,disappointed;' +
            'Frowning face with open mouth~shocked,frown,unhappy;Anguished face~shocked,pain,anguish;Fearful face~scared,afraid,fear;Weary face~tired,sigh,exhausted;Sleepy face~tired,snot,zzz;' +
            'Tired face~exhausted,sleepy;Grimacing face~awkward,oops,eek;Loudly crying face~cry,sob,sad;Face with open mouth~wow,surprised,oh;Hushed face~surprised,wow,quiet;' +
            'Anxious face with sweat~nervous,scared;Face screaming in fear~scream,scared,shock;Astonished face~wow,shocked,amazed;Flushed face~embarrassed,blush,shy;' +
            'Sleeping face~sleep,tired,zzz;Dizzy face~knocked out,dead,spiral;Face without mouth~silent,speechless,quiet;Face with medical mask~sick,ill,doctor;@1F641;' +
            'Slightly frowning face~sad,frown,unhappy;Slightly smiling face~smile,happy,ok;Upside-down face~silly,sarcasm,irony;Face with rolling eyes~eyeroll,annoyed,whatever;@1F910;' +
            'Zipper-mouth face~secret,quiet,shh;Money-mouth face~rich,cash,dollar;Face with thermometer~sick,ill,fever;Nerd face~geek,smart,glasses;Thinking face~think,hmm,wonder;' +
            'Face with head-bandage~hurt,injured,ouch;@1F917;Hugging face~hug,thanks,warm;@1F920;Cowboy hat face~western,yeehaw;Clown face~joker,silly,circus;Nauseated face~sick,ill,vomit;' +
            'Rolling on the floor laughing~rofl,lol,laugh;Drooling face~drool,hungry,want;Lying face~liar,lie,pinocchio;@1F927;Sneezing face~sick,achoo,cold;@1F638;' +
            'Grinning cat with smiling eyes;Cat with tears of joy;Grinning cat;Smiling cat with heart-eyes;Cat with wry smile;Kissing cat;Pouting cat;Crying cat;Weary cat;@1F648;' +
            'See-no-evil monkey~shy,embarrassed,oops;Hear-no-evil monkey~deaf;Speak-no-evil monkey~secret,quiet,oops;@1F479;Ogre~monster,oni,demon;Goblin~monster,tengu,demon;' +
            'Ghost~boo,spooky,halloween;@1F47D;Alien~ufo,space;Alien monster~space invader,game,arcade;Angry face with horns~devil,demon,evil;Skull~dead,death,bones;@1F4A9;' +
            'Pile of poo~poop,dung;@1F916;Robot~bot,machine,ai;@1F48B;Kiss mark~lips,love,lipstick;Love letter~mail,heart,valentine;@1F493;Beating heart~love,pulse;Broken heart~sad,heartbreak;' +
            'Two hearts~love,heart,couple;Sparkling heart~love,sparkle;Growing heart~love;Heart with arrow~love,cupid,valentine;Blue heart~love;Green heart~love;Yellow heart~love;' +
            'Purple heart~love;Heart with ribbon~love,gift;Revolving hearts~love,heart,circle;Heart decoration~love;@1F4A2;Anger symbol~angry,mad,vein;@1F4A4;Zzz~sleep,tired,snore;' +
            'Collision~boom,bang,crash;Sweat droplets~drops,water,splash;@1F4A8;Dashing away~fast,wind,dash;@1F4AB;Dizzy~star,stars;Speech balloon~chat,talk,message;' +
            'Thought balloon~think,dream,idea;@1F4AF;Hundred points~perfect,score,100;@1F5A4;Black heart~love,dark'],
        ['people', 'People',
            '@1F446;Pointing up~point,above;Pointing down~point,below;Pointing left~point,back;Pointing right~point,next;Oncoming fist~punch,bump,hit;Waving hand~hi,hello,bye;' +
            'OK hand~perfect,good,fine;Thumbs up~like,yes,good;Thumbs down~dislike,no,bad;Clapping hands~clap,applause,bravo;Open hands~hug,jazz hands;@1F4AA;Flexed biceps~strong,muscle,arm;' +
            '@1F595;Middle finger~rude;Vulcan salute~spock,star trek,live long;@1F64C;Raising hands~hooray,yay,celebrate;@1F64F;Folded hands~please,thanks,pray;@1F918;' +
            'Sign of the horns~rock,metal,rock on;Call me hand~phone,shaka,surf;Raised back of hand;Left-facing fist;Right-facing fist;Handshake~deal,agreement,shake;' +
            'Crossed fingers~luck,hope,wish;@270A;Raised fist~power,solidarity,punch;Raised hand~stop,high five,palm;@1F440;Eyes~look,see,watch;@1F442;Ear~hear,listen,sound;Nose~smell,sniff;' +
            'Mouth~lips,kiss;Tongue~lick,taste;@1F463;Footprints~feet,walk,steps;@1F485;Nail polish~nails,beauty,manicure;@1F385;Santa Claus~christmas,xmas,holiday;@1F3C2;Snowboarder;' +
            'Person running~run,jog,race;Person surfing~surf,wave,beach;@1F3C7;Horse racing;@1F3CA;Person swimming~swim,pool,water;@1F464;Bust in silhouette;Busts in silhouette;Boy~child,kid;' +
            'Girl~child,kid;Man~male,guy,dude;Woman~female,lady,girl;Family~parents,kids;Woman and man holding hands;Two men holding hands;Two women holding hands;Police officer~cop,law;' +
            'People with bunny ears~party,bunnies,dance;Person with veil;Person with blond hair;Person with skullcap;Person wearing turban;Old man~grandpa,elder,senior;' +
            'Old woman~grandma,elder,senior;Baby~child,infant;Construction worker~builder,hard hat;Princess~queen,royal,crown;@1F47C;Baby angel~heaven,holy;@1F481;Person tipping hand;Guard;' +
            'Woman dancing~dance,party,flamenco;Lipstick;@1F486;Person getting massage;Person getting haircut;@1F48F;Kiss~love,couple,romance;@1F491;Couple with heart~love,romance,relationship;' +
            '@1F57A;Man dancing~dance,party,disco;@1F645;Person gesturing no;Person gesturing OK;Person bowing;@1F64B;Person raising hand;@1F64D;Person frowning;Person pouting;@1F6B4;' +
            'Person biking~bike,cycle,ride;Person mountain biking;Person walking~walk,hike,stroll;@1F6C0;Person taking bath;@1F6CC;Person in bed~sleep,rest,night;@1F926;' +
            'Person facepalming~facepalm,fail,embarrassed;@1F930;Pregnant woman;@1F933;Selfie;Prince~king,royal,crown;Person in tuxedo;Mrs. Claus;Person shrugging~shrug,idk,dunno;' +
            'Person cartwheeling;Person juggling;Person fencing;@1F93C;People wrestling;Person playing water polo;Person playing handball;@1F392;Backpack~school,bag,student;' +
            'Graduation cap~school,graduate,degree;@1F3A9;Top hat~magic,fancy,gentleman;@1F451;Crown~king,queen,royal;Woman\'s hat;Glasses~nerd,eyes,spectacles;Necktie;T-shirt~clothes,tee;' +
            'Jeans~pants,clothes,denim;Dress~clothes,fashion;Kimono;Bikini~swim,beach,summer;Woman\'s clothes;Purse;Handbag;Pouch;Man\'s shoe;Running shoe~sneaker,sport,run;High-heeled shoe;' +
            'Woman\'s sandal;Woman\'s boot;@1F48D;Ring~wedding,engaged,marry'],
        ['animals', 'Animals and nature',
            '@1F400;Rat~rodent;Mouse~rodent;Ox;Water buffalo;Cow;Tiger;Leopard;Rabbit~bunny,pet;Cat~pet,kitty,meow;Dragon~fantasy,fire,monster;Crocodile~alligator,reptile;Whale~sea,ocean;' +
            'Snail~slow,slug,shell;Snake~reptile,serpent,hiss;Horse~ride,farm;Ram;Goat;Sheep;Monkey~ape,primate;Rooster;Chicken~hen,farm,bird;Dog~pet,puppy,woof;Pig;Boar;Elephant~animal,trunk;' +
            'Octopus~sea,ocean,tentacle;Spiral shell;Bug~insect,caterpillar,worm;Ant~insect,bug,colony;Honeybee~bee,insect,buzz;Lady beetle~ladybug,ladybird,insect;Fish~sea,ocean;Tropical fish;' +
            'Blowfish;Turtle~slow,tortoise,reptile;Hatching chick;Baby chick;Baby chick facing front;Bird~tweet,sparrow;Penguin~bird,cold,linux;Koala~australia,bear;Poodle~dog,pet;Camel;' +
            'Two-hump camel;Dolphin~sea,ocean,flipper;Mouse face~pet,rodent;Cow face~farm,moo;Tiger face~cat,animal;Rabbit face~bunny,pet;Cat face~pet,kitty,meow;' +
            'Dragon face~fantasy,fire,monster;Spouting whale~sea,ocean,spout;Horse face~ride,farm,pony;Monkey face~ape,primate;Dog face~pet,puppy,woof;Pig face~farm,oink;Frog face~toad,ribbit;' +
            'Hamster face~pet,rodent;Wolf face;Bear face~animal,teddy;Panda face~bear,china;Pig nose;Paw prints~paws,pet,animal;@1F980;Crab~sea,ocean,seafood;Lion~king,cat,leo;' +
            'Scorpion~sting,arachnid;Turkey;Unicorn~magic,fantasy,horse;Eagle~bird,usa,freedom;Duck;Bat~vampire,night;Shark~sea,ocean,jaws;Owl~bird,night,wise;Fox~animal,clever;' +
            'Butterfly~insect,pretty;Deer;Gorilla;Lizard~reptile,gecko;Rhinoceros;Shrimp;Squid;@1F331;Seedling~plant,sprout,grow;Evergreen tree~pine,forest,fir;Deciduous tree~forest,nature;' +
            'Palm tree~beach,tropical;Cactus~desert,plant;@1F337;Tulip~flower,spring,pink;Cherry blossom~flower,spring,pink;Rose~flower,love,red;Hibiscus;Sunflower~flower,sun,yellow;Blossom;' +
            '@1F33E;Sheaf of rice;Herb~plant,leaf,green;Four leaf clover~luck,lucky,irish;Maple leaf~autumn,fall,canada;Fallen leaf~autumn,fall;Leaf fluttering in wind;' +
            'Mushroom~fungus,toadstool;@1F490;Bouquet~flowers,gift,love;@1F4AE;White flower;@1F940;Wilted flower'],
        ['food', 'Food and drink',
            '@1F32D;Hot dog~hotdog,food,sausage;Taco~food,mexican;Burrito~food,mexican,wrap;Chestnut;@1F33D;Ear of corn;@1F345;Tomato~vegetable;Eggplant;Grapes~fruit,wine;Melon;' +
            'Watermelon~fruit,summer;Tangerine~orange,fruit;Lemon;Banana~fruit,yellow;Pineapple;Red apple~fruit;Green apple~fruit;Pear;Peach;Cherries~cherry,fruit;Strawberry~fruit,berry,red;' +
            'Hamburger~burger,food,fast food;Pizza~food,slice,italian;Meat on bone;Poultry leg;Rice cracker;Rice ball;Cooked rice~food,bowl;Curry and rice;Steaming bowl~noodles,ramen,soup;' +
            'Spaghetti~pasta,italian,food;Bread~loaf,toast;French fries~chips,food,fast food;Roasted sweet potato;Dango;Oden;Sushi~japanese,fish,food;Fried shrimp;Fish cake with swirl;' +
            'Soft ice cream~dessert,sweet,summer;Shaved ice;Ice cream~dessert,sweet;Doughnut~donut,dessert,sweet;Cookie~biscuit,dessert,sweet;Chocolate bar~candy,sweet;Candy~sweet,sugar;' +
            'Lollipop~candy,sweet,sugar;Custard;Honey pot~sweet,bee;Shortcake~cake,dessert,sweet;Bento box;Pot of food;Cooking~egg,fry,pan;Fork and knife~eat,dinner,food;Teacup~tea,hot,drink;' +
            'Sake;Wine glass~drink,alcohol,red;Cocktail glass~drink,martini,alcohol;Tropical drink~cocktail,holiday;Beer mug~drink,pub,cheers;Clinking beer mugs~cheers,drink,toast;Baby bottle;' +
            '@1F37E;Bottle with popping cork~champagne,party,celebrate;Popcorn~movie,snack,cinema;@1F382;Birthday cake~party,dessert;@1F3FA;Amphora;@1F52A;Kitchen knife~cut,hocho,weapon;@1F942;' +
            'Clinking glasses~cheers,toast,champagne;Tumbler glass~whiskey,drink,alcohol;Spoon;@1F950;Croissant~bread,french,breakfast;Avocado;Cucumber;Bacon~breakfast,meat;Potato;Carrot;' +
            'Baguette;Green salad;Shallow pan of food;Stuffed flatbread;Egg~breakfast,food;Glass of milk~drink;Peanuts;Kiwi fruit;Pancakes;@1F9C0;Cheese wedge;@2615;Hot drink~coffee,tea,cafe'],
        ['activities', 'Activities and games',
            '@1F004;Mahjong red dragon~tile,game;@1F0CF;Joker~card,game,wild;@1F380;Ribbon~bow,gift,cute;Gift~present,birthday,surprise;@1F383;Jack-o-lantern~halloween,pumpkin,spooky;' +
            'Christmas tree~xmas,holiday;@1F386;Fireworks~celebrate,new year,party;Sparkler~celebrate,fireworks;Balloon~party,birthday;Party popper~celebrate,congrats,tada;' +
            'Confetti ball~party,celebrate;Tanabata tree;Crossed flags~japan;Pine decoration;Japanese dolls;Carp streamer;Wind chime;Moon viewing ceremony;@1F3A3;Fishing pole~rod,fish,catch;' +
            '@1F3A8;Artist palette~art,paint,draw;@1F3AB;Ticket~event,entry;@1F3AD;Performing arts~theater,drama,masks;Video game~gaming,controller,play;Bullseye~target,dart,aim;' +
            'Slot machine~casino,gamble,jackpot;Billiards~pool,eight ball,8 ball;Game die~dice,roll,random;Bowling;Flower playing cards;@1F3BD;Running shirt;Tennis~sport,racket;Skis;' +
            'Basketball~sport,ball,hoop;Chequered flag~race,finish,checkered;@1F3C5;Sports medal~win,winner,prize;Trophy~win,winner,champion;@1F3C8;American football~sport,ball;Rugby football;' +
            '@1F3CF;Cricket game;Volleyball;Field hockey;Ice hockey;Ping pong~table tennis,paddle,sport;@1F3F4;Black flag~pirate;@1F3F8;Badminton~sport,shuttlecock;@1F52B;' +
            'Water pistol~gun,shoot,toy;@1F52E;Crystal ball~fortune,magic,future;@1F6A9;Triangular flag~marker,red,post;@1F945;Goal net~score,sport;@1F947;First place medal~gold,win,winner;' +
            'Second place medal~silver;Third place medal~bronze;Boxing glove~fight,punch;Martial arts uniform~karate,judo;@26BD;Soccer ball~football,sport;Baseball~sport,ball;@26F3;Golf;@2728;' +
            'Sparkles~magic,new,shiny'],
        ['travel', 'Travel and places',
            '@1F300;Cyclone~spiral,hurricane,dizzy;Foggy;Closed umbrella;Night with stars;Sunrise over mountains;Sunrise;Cityscape at dusk;Sunset;Rainbow~colors,pride,weather;Bridge at night;' +
            'Water wave~sea,ocean,surf;Volcano~mountain,lava,eruption;Milky way;Globe showing Europe and Africa~earth,world,planet;Globe showing the Americas~earth,world,planet;' +
            'Globe showing Asia and Australia~earth,world,planet;Globe with meridians~world,web,internet;New moon;Waxing crescent moon;First quarter moon;Waxing gibbous moon;Full moon;' +
            'Waning gibbous moon;Last quarter moon;Waning crescent moon;Crescent moon~night,sleep;New moon with face;First quarter moon with face;Last quarter moon with face;' +
            'Full moon with face~night;Sun with face~day,bright,sunny;Glowing star~shine,glow,sparkle;Shooting star~wish,night;@1F3A0;Carousel horse;Ferris wheel~fair,park;' +
            'Roller coaster~fair,park,ride;@1F3AA;Circus tent;@1F3E0;House~home,building;House with garden~home;Office building~work;Japanese post office;Post office;Hospital~health,doctor;' +
            'Bank;ATM sign;Hotel;Love hotel;Convenience store;School~class,building,education;Department store;Factory;Red paper lantern;Japanese castle;Castle~fort,medieval,fantasy;@1F488;' +
            'Barber pole;@1F492;Wedding;@1F4A7;Droplet~water,drop,sweat;@1F4BA;Seat;@1F525;Fire~hot,flame,lit;@1F54B;Kaaba;Mosque;Synagogue;Menorah;@1F550;One o\'clock~time,hour;Two o\'clock;' +
            'Three o\'clock;Four o\'clock;Five o\'clock;Six o\'clock;Seven o\'clock;Eight o\'clock;Nine o\'clock;Ten o\'clock;Eleven o\'clock;Twelve o\'clock;One-thirty;Two-thirty;Three-thirty;' +
            'Four-thirty;Five-thirty;Six-thirty;Seven-thirty;Eight-thirty;Nine-thirty;Ten-thirty;Eleven-thirty;Twelve-thirty;@1F5FB;Mount Fuji~mountain,japan;Tokyo tower;Statue of Liberty;' +
            'Map of Japan;Moai~stone,statue,face;@1F680;Rocket~space,launch,fast;Helicopter;Steam locomotive~train,railway;Railway car;High-speed train;Bullet train;Train~railway,transport;' +
            'Metro;Light rail;Station;Tram;Tram car;Bus~transport,public;Oncoming bus;Trolleybus;Bus stop;Minibus;Ambulance~emergency,hospital,rescue;Fire engine~fire truck,emergency,rescue;' +
            'Police car~cop,law;Oncoming police car;Taxi~cab,ride;Oncoming taxi;Automobile~car,drive,vehicle;Oncoming automobile;Recreational vehicle;Delivery truck;Articulated lorry;Tractor;' +
            'Monorail;Mountain railway;Suspension railway;Mountain cableway;Aerial tramway;Ship~boat,cruise,sea;Rowboat;Speedboat~boat,sea,fast;Horizontal traffic light;' +
            'Vertical traffic light~signal;Construction sign~roadwork,work;Police car light;@1F6B2;Bicycle~bike,cycle,ride;@1F6D1;Stop sign~halt,octagon;@1F6EB;' +
            'Airplane departure~plane,flight,takeoff;Airplane arrival~plane,flight,landing;@1F6F4;Kick scooter;Motor scooter;Canoe;@2614;Umbrella with rain~weather,wet;@2693;Anchor;@26A1;' +
            'High voltage~lightning,thunder,bolt;@26C4;Snowman~snow,winter,cold;Sun behind cloud~weather,partly cloudy;@26EA;Church~religion,building;@26F2;Fountain~park,water;@26F5;' +
            'Sailboat~boat,sail,sea;@26FA;Tent~camp,camping,outdoors;@26FD;Fuel pump;@2B50;Star~favorite,gold,rating'],
        ['objects', 'Objects',
            '@1F3A4;Microphone~mic,sing,karaoke;Movie camera~film,cinema;Cinema;Headphone~headphones,music,listen;@1F3AC;Clapper board~movie,film,action;@1F3B5;Musical note~music,song,tune;' +
            'Musical notes~music,song,tune;Saxophone~music,jazz;Guitar~music,rock,band;Musical keyboard~piano,music;Trumpet~music,horn;Violin~music,fiddle;Musical score;@1F3F9;' +
            'Bow and arrow~archery,shoot,weapon;@1F489;Syringe~shot,vaccine,needle;Pill~medicine,drug,health;@1F48E;Gem stone~diamond,jewel,rich;@1F4A1;Light bulb~idea,lamp,bright;@1F4A3;' +
            'Bomb~boom,explode,danger;@1F4B0;Money bag~rich,cash,treasure;@1F4B3;Credit card~payment,buy,money;Yen banknote;Dollar banknote~money,cash,bill;Euro banknote;Pound banknote;' +
            'Money with wings~fly,spend,loss;@1F4BB;Personal computer~laptop,pc,work;Briefcase;Computer disk;Floppy disk;Optical disk;DVD;File folder;Open file folder;Page with curl;' +
            'Page facing up;Calendar~date,schedule;Tear-off calendar;Card index;Chart increasing~up,growth,stocks;Chart decreasing~down,decline,stocks;Bar chart;Clipboard;' +
            'Pushpin~pin,mark,location;Round pushpin;Paperclip~attach,clip;Straight ruler;Triangular ruler;Bookmark tabs;Ledger;Notebook;Notebook with decorative cover;Closed book;' +
            'Open book~read,study;Green book;Blue book;Orange book;Books~read,study,library;Name badge;Scroll;Memo~note,write,pencil;Telephone receiver~phone,call;Pager;Fax machine;' +
            'Satellite antenna;Loudspeaker~announce,speaker,shout;Megaphone~announce,shout,cheer;Outbox tray;Inbox tray;Package~box,delivery,parcel;E-mail~email,message,letter;' +
            'Incoming envelope;Envelope with arrow;Closed mailbox with lowered flag;Closed mailbox with raised flag;Open mailbox with raised flag;Open mailbox with lowered flag;Postbox;' +
            'Postal horn;Newspaper;Mobile phone~cell,call,smartphone;Mobile phone with arrow;@1F4F7;Camera~photo,picture;Camera with flash~photo,picture;Video camera~film,record;' +
            'Television~tv,watch,screen;Radio~music,fm;Videocassette;@1F4FF;Prayer beads;@1F507;Muted speaker~mute,silent,off;Speaker;Speaker with one wave;' +
            'Speaker with three waves~loud,sound,volume;Battery~power,charge,energy;Electric plug~power,charger;Magnifying glass tilted left~search,find,magnifier;' +
            'Magnifying glass tilted right~search,find,magnifier;Locked with pen;Locked with key;Key~lock,password,secret;Locked~lock,secure,private;Unlocked~unlock,open;' +
            'Bell~notification,alert,ring;Bell with slash~mute,silent,off;Bookmark~tag,save;Link~chain,url,connect;@1F526;Flashlight~torch,light,dark;Wrench~tool,fix,repair;' +
            'Hammer~tool,build,fix;Nut and bolt~tool,screw,fix;@1F52C;Microscope~science,lab,research;Telescope~space,stars,science;@1F6AA;Door~enter,exit,room;@1F6AC;Cigarette~smoke,smoking;' +
            '@1F6BD;Toilet~wc,bathroom,loo;@1F6BF;Shower~wash,bath,water;@1F6C1;Bathtub~bath,tub,wash;@1F6D2;Shopping cart~store,buy,trolley;@1F941;Drum~music,beat,band;@231A;Watch~time,clock;' +
            'Hourglass~wait,time,sand;@23F0;Alarm clock~time,wake up;@23F3;Hourglass with sand~wait,time,timer'],
        ['symbols', 'Symbols',
            '@1F18E;AB button~blood,type;@1F191;CL button~clear;Cool button~nice,awesome;Free button~gratis,no cost;ID button~identity,card;New button~fresh,latest;NG button~no good;' +
            'OK button~okay,fine,good;SOS button~help,emergency,rescue;Up button~level up,upgrade;VS button~versus,fight,battle;@1F201;Here button;@1F21A;Free of charge button;@1F22F;' +
            'Reserved button;@1F232;Prohibited button;Vacancy button;Passing grade button;No vacancy button;Not free of charge button;@1F238;Application button;Discount button;' +
            'Open for business button;@1F250;Bargain button;Accept button;@1F4A0;Diamond with a dot~shape;@1F4B1;Currency exchange~money,forex;Dollar sign~money,cash,price;@1F4B9;' +
            'Chart increasing with yen~stocks,market,trend;@1F4F3;Vibration mode;Mobile phone off;No mobile phones~silent,ban,off;Antenna bars;@1F500;Shuffle~random,mix;Repeat~loop,again;' +
            'Repeat single~loop,once;Clockwise arrows~refresh,reload,loop;Counterclockwise arrows~refresh,reload,sync;Dim button;Bright button;@1F518;Radio button;Back arrow~return,previous;' +
            'End arrow~finish,last;On arrow~active,start;Soon arrow~coming,later,next;Top arrow~up,best,highest;No one under eighteen~adult,18,nsfw;Keycap 10~ten,number;' +
            'Input latin uppercase~abc,letters,text;Input latin lowercase~abc,letters,text;Input numbers~digits,123,count;Input symbols~characters;Input latin letters~abc,alphabet,text;@1F52F;' +
            'Dotted six-pointed star~david;Japanese symbol for beginner~new,starter;Trident emblem~spear,poseidon;Black square button;White square button;Red circle~dot;Blue circle~dot;' +
            'Large orange diamond~shape;Large blue diamond~shape;Small orange diamond;Small blue diamond;Red triangle pointed up~increase;Red triangle pointed down~decrease;Upwards button;' +
            'Downwards button;@1F6AB;Prohibited~no,forbidden,banned;@1F6AD;No smoking~ban,smoke;Litter in bin sign;No littering~litter,trash,ban;Potable water;Non-potable water;@1F6B3;' +
            'No bicycles~no bikes,ban;@1F6B7;No pedestrians~no walking,ban;Children crossing~kids,school,warning;Men\'s room~man,male,toilet;Women\'s room~woman,female,toilet;' +
            'Restroom~toilet,wc,bathroom;Baby symbol~child,infant;@1F6BE;Water closet;@1F6C2;Passport control~customs,airport,border;Customs;Baggage claim;Left luggage;@1F6D0;' +
            'Place of worship~religion,pray,temple;@23E9;Fast-forward~skip,next,speed;Rewind~back,previous;Fast up~top,increase;Fast down~bottom,decrease;@25FD;Small white square;' +
            'Small black square;@2648;Aries~zodiac,ram,horoscope;Taurus~zodiac,bull,horoscope;Gemini~zodiac,twins,horoscope;Cancer~zodiac,crab,horoscope;Leo~zodiac,lion,horoscope;' +
            'Virgo~zodiac,maiden,horoscope;Libra~zodiac,scales,horoscope;Scorpius~zodiac,scorpio,scorpion;Sagittarius~zodiac,archer,horoscope;Capricorn~zodiac,goat,horoscope;' +
            'Aquarius~zodiac,water bearer,horoscope;Pisces~zodiac,fish,horoscope;@267F;Wheelchair~access,disabled,accessible;@26AA;White circle~dot;Black circle~dot;@26CE;' +
            'Ophiuchus~zodiac,snake bearer,horoscope;@26D4;No entry~stop,forbidden,wrong way;@2705;Check mark button~yes,done,tick;@274C;Cross mark~no,wrong,x;@274E;' +
            'Cross mark button~no,x,cancel;@2753;Red question mark~why,what,how;White question mark~why,what,how;White exclamation mark~alert,attention;@2757;' +
            'Red exclamation mark~warning,alert,important;@2795;Plus~add,more,math;Minus~subtract,less,math;Divide~math,division;@27B0;Curly loop~wave;@27BF;Double curly loop;@2B1B;' +
            'Black square~box;White square~box;@2B55;Hollow red circle~ring,correct,ok']
    ];
    // the pictures: inline SVG made node by node (24 x 24, drawn in the button's own color); [element, its attributes]
    var ICONS = {
        face: [['circle', 'cx:12;cy:12;r:9'], ['circle', 'cx:9;cy:10;r:1;class:gt-eye'], ['circle', 'cx:15;cy:10;r:1;class:gt-eye'], ['path', 'd:M8 14.5c1 1.6 2.4 2.5 4 2.5s3-.9 4-2.5']],
        mic: [['rect', 'x:9;y:3;width:6;height:11;rx:3;class:gt-cap'], ['path', 'd:M5 11a7 7 0 0 0 14 0M12 18v3M9 21h6']],
        recent: [['circle', 'cx:12;cy:12;r:9'], ['path', 'd:M12 7v5l3 2']],
        people: [['circle', 'cx:12;cy:8;r:3.5'], ['path', 'd:M5 20c0-3.9 3.1-6 7-6s7 2.1 7 6']],
        paw: [['circle', 'cx:6;cy:11;r:1.6'], ['circle', 'cx:9.5;cy:6.5;r:1.6'], ['circle', 'cx:14.5;cy:6.5;r:1.6'], ['circle', 'cx:18;cy:11;r:1.6'], ['path', 'd:M12 12c-3 0-5.5 2.8-5.5 5.2 0 1.5 1.2 2.3 2.7 2.3 1 0 1.7-.5 2.8-.5s1.8.5 2.8.5c1.5 0 2.7-.8 2.7-2.3C17.5 14.8 15 12 12 12z']],
        apple: [['path', 'd:M12 8c-2-2-6-1-6.5 3.5C5 15.5 8 20 10 20c1 0 1.5-.5 2-.5s1 .5 2 .5c2 0 5-4.5 4.5-8.5C18 7 14 6 12 8z'], ['path', 'd:M12 8c0-2 1-3.5 3-4']],
        pad: [['path', 'd:M7 8h10a4 4 0 0 1 4 4v1.5a3.5 3.5 0 0 1-6 2.5l-1-1h-4l-1 1a3.5 3.5 0 0 1-6-2.5V12a4 4 0 0 1 4-4z'], ['path', 'd:M8 10.5v3M6.5 12h3'], ['circle', 'cx:16;cy:11.5;r:.8'], ['circle', 'cx:18;cy:13.5;r:.8']],
        pin: [['path', 'd:M12 21s-6.5-5.7-6.5-11a6.5 6.5 0 0 1 13 0c0 5.3-6.5 11-6.5 11z'], ['circle', 'cx:12;cy:10;r:2.3']],
        bulb: [['path', 'd:M9 18h6M10 21h4'], ['path', 'd:M12 3a6 6 0 0 0-3.5 10.9c.6.5 1 1.2 1 2.1h5c0-.9.4-1.6 1-2.1A6 6 0 0 0 12 3z']],
        heart: [['path', 'd:M12 20s-7.5-4.6-7.5-10.2A4.3 4.3 0 0 1 12 7.3a4.3 4.3 0 0 1 7.5 2.5C19.5 15.4 12 20 12 20z']],
        lens: [['circle', 'cx:11;cy:11;r:6'], ['path', 'd:M16 16l4.5 4.5']],
        cross: [['path', 'd:M7 7l10 10M17 7L7 17']]
    };
    var TABS = ['recent', 'face', 'people', 'paw', 'apple', 'pad', 'pin', 'bulb', 'heart'];       // the pictures of the tabs: the recently used ones, then the eight groups in order

    function chr(cp) { return cp < 0x10000 ? String.fromCharCode(cp) : String.fromCharCode(0xd800 + ((cp - 0x10000) >> 10), 0xdc00 + ((cp - 0x10000) & 0x3ff)); }
    function hex(ch) { var c = ch.charCodeAt(0); if (c >= 0xd800 && c <= 0xdbff) c = 0x10000 + ((c - 0xd800) << 10) + (ch.charCodeAt(1) - 0xdc00); return c.toString(16).toUpperCase(); }
    function words(s) { return String(s).toLowerCase().match(/[a-z0-9]+/g) || []; }
    // the table of the emoji: the groups ({ id, name, list: the characters, names, keys: the search words of each }), and for each character its entry ({ ch, name, keys, nw, kw }: the words of the name and of the search words)
    function table() {
        var all = [], by = {}, groups = GROUPS.map(function (g) {
            var out = { id: g[0], name: g[1], list: [], names: [], keys: [], items: [] }, code = 0;
            g[2].split(';').forEach(function (p) {
                if (p.charAt(0) === '@') { code = parseInt(p.slice(1), 16); return; }
                var i = p.indexOf('~'), it = { ch: chr(code++), name: i < 0 ? p : p.slice(0, i), keys: i < 0 ? [] : p.slice(i + 1).split(',') };
                it.nw = words(it.name); it.kw = words(it.keys.join(' '));
                out.list.push(it.ch); out.names.push(it.name); out.keys.push(it.keys); out.items.push(it); all.push(it); by[it.ch] = it;
            });
            return out;
        });
        return { groups: groups, all: all, by: by };
    }
    function emoji() { return table().groups.map(function (g) { return { id: g.id, name: g.name, list: g.list, names: g.names, keys: g.keys }; }); }
    // how well an entry answers one word of the search: 0 a word of the name is it, 1 a search word is it, 2 a word of the name starts with it, 3 a search word of 4 letters or more starts with it; -1 it does not
    function rank(it, t) {
        var best = -1, i, v;
        for (i = 0; i < it.nw.length; i++) { v = it.nw[i] === t ? 0 : it.nw[i].indexOf(t) === 0 ? 2 : -1; if (v !== -1 && (best === -1 || v < best)) best = v; }
        for (i = 0; i < it.kw.length; i++) { v = it.kw[i] === t ? 1 : t.length > 3 && it.kw[i].indexOf(t) === 0 ? 3 : -1; if (v !== -1 && (best === -1 || v < best)) best = v; }
        return best;
    }
    // the entries that answer every word of `query`, the best first, then the shorter names (a plural counts as its singular); none for a query with no word in it
    function search(tab, query) {
        var t = words(query), out = [], i, j, s, r;
        for (i = 0; t.length && i < tab.all.length; i++) {
            for (s = 0, j = 0; j < t.length && s !== -1; j++) {
                r = rank(tab.all[i], t[j]);
                if (r === -1 && t[j].length > 3 && t[j].charAt(t[j].length - 1) === 's') r = rank(tab.all[i], t[j].slice(0, -1));
                s = r === -1 ? -1 : s + r;
            }
            if (s !== -1) out.push({ s: s, i: i });
        }
        return out.sort(function (a, b) { return a.s - b.s || tab.all[a.i].nw.length - tab.all[b.i].nw.length || a.i - b.i; }).map(function (x) { return tab.all[x.i]; });
    }

    function points(s) { var n = 0; for (var i = 0; i < s.length; i++) { var c = s.charCodeAt(i); if (!(c >= 0xdc00 && c <= 0xdfff)) n++; } return n; }
    function head(s, n) {                                      // the first n code points of s (a pair of surrogates is never cut)
        var i = 0, k = 0;
        while (i < s.length && k < n) { var c = s.charCodeAt(i); i += c >= 0xd800 && c <= 0xdbff && i + 1 < s.length ? 2 : 1; k++; }
        return s.slice(0, i);
    }
    // `text` into `value` between the caret's two ends: the new value and the caret after it, or null when the line would pass 200 code points
    function insert(value, from, to, text, limit) {
        var a = typeof from === 'number' && from >= 0 && from <= value.length ? from : value.length, b = typeof to === 'number' && to >= a && to <= value.length ? to : a;
        var held = a === b;
        if (a > 0 && a < value.length && /[\udc00-\udfff]/.test(value.charAt(a)) && /[\ud800-\udbff]/.test(value.charAt(a - 1))) a--;       // (a caret between the two halves of a pair goes before it)
        if (held) b = a;
        else if (b < value.length && /[\udc00-\udfff]/.test(value.charAt(b)) && /[\ud800-\udbff]/.test(value.charAt(b - 1))) b++;
        var next = value.slice(0, a) + text + value.slice(b);
        return points(next) > (limit || LIMIT) ? null : { value: next, caret: a + text.length };
    }
    // what is said, into the field around the place where the person was: no markup characters (the hub refuses them), one space between words, at most what is left of the 200 code points
    function speak(before, said, after, limit) {
        var t = String(said).replace(/[<>]/g, ' ').replace(/\s+/g, ' ').trim();
        if (!t) return { value: before + after, caret: before.length };
        t = head(t, Math.max(0, (limit || LIMIT) - points(before + after) - 2)).replace(/\s+$/, '');
        if (!t) return { value: before + after, caret: before.length };       // (nothing of it fits: no spaces are added either)
        var lead = before && !/\s$/.test(before) ? ' ' : '', tail = after && !/^\s/.test(after) ? ' ' : '';
        return { value: before + lead + t + tail + after, caret: (before + lead + t).length };
    }
    function recognition(win) {                                // the browser's constructor, or null: none, or a page that is not secure (the microphone is not offered there)
        if (!win || win.isSecureContext !== true) return null;
        var C = win.SpeechRecognition || win.webkitSpeechRecognition;
        return typeof C === 'function' ? C : null;
    }

    // the page's own permissions policy (the Permissions-Policy header of docker/nginx.conf): true only when this browser can tell and says the microphone is not allowed; a browser without the API, or one that throws, is "not known"
    function policyBlocks(doc) {
        try {
            var p = doc && (doc.permissionsPolicy || doc.featurePolicy);
            return !!(p && typeof p.allowsFeature === 'function' && p.allowsFeature('microphone') === false);
        } catch (e) { return false; }
    }

    function mount(env) {
        var doc = env.doc, win = env.win, dock = env.dock, field = dock.field, form = field.parentNode;
        if (!form) return null;
        var max = env.limit || LIMIT, withEmoji = env.emoji !== false;       // (the room's chat line is 100 printable ASCII characters: it passes no emoji and its own limit)
        function el(tag, cls, text) { var e = doc.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; }
        function button(cls, label, text) { var b = el('button', 'btn ' + cls, text); b.type = 'button'; b.setAttribute('aria-label', label); b.setAttribute('title', label); return b; }
        // the pictures: inline SVG made node by node (no file, no font, no markup string), drawn in the button's own color: a smiley in a circle, the usual microphone (a capsule, the holder under it, a stem and a base), and the line pictures of the picker
        var NS = ['http' + ':', '', 'www.w3.org', '2000', 'svg'].join('/');       // (the SVG namespace: a name, not an address that is asked for)
        function part(tag, attrs, into) { var n = doc.createElementNS ? doc.createElementNS(NS, tag) : doc.createElement(tag); attrs.split(';').forEach(function (a) { n.setAttribute(a.split(':')[0], a.split(':')[1]); }); if (into) into.appendChild(n); return n; }
        function icon(kind) {
            var svg = part('svg', 'viewBox:0 0 24 24;class:gt-ico;aria-hidden:true;focusable:false');
            ICONS[kind].forEach(function (p) { part(p[0], p[1], svg); });
            return svg;
        }
        var send = null, i;
        for (i = 0; i < form.children.length; i++) if (String(form.children[i].className).indexOf('gc-send') !== -1) { send = form.children[i]; break; }
        var tools = el('span', 'gt-tools'), emojiBtn = button('gt-btn gt-emoji', TEXT.emoji, ''), micBtn = null;
        emojiBtn.appendChild(icon('face')); emojiBtn.setAttribute('aria-expanded', 'false'); emojiBtn.setAttribute('aria-controls', 'gt-pick');
        if (withEmoji) tools.appendChild(emojiBtn);
        var Ctor = recognition(win);
        if (Ctor) { micBtn = button('gt-btn gt-mic', TEXT.voice, ''); micBtn.appendChild(icon('mic')); micBtn.setAttribute('aria-pressed', 'false'); tools.appendChild(micBtn); }
        var say = el('p', 'gt-say'); say.setAttribute('role', 'status'); say.setAttribute('aria-live', 'polite'); say.hidden = true;
        var pick = el('div', 'gt-pick'); pick.id = 'gt-pick'; pick.setAttribute('role', 'group'); pick.setAttribute('aria-label', TEXT.emojiList); pick.hidden = true;
        var T = withEmoji ? table() : null, cells = [], shown = [], tabBtns = [], recent = null, tab = 1, query = '', found = [], at = 0, mode = '';
        var lastW = 0, held = null, q = null, clear = null, body = null, recentBox = null, recentGrid = null, pageCap = null, pageGrid = null, big = null, nm = null, kw = null;
        if (withEmoji) {
            var top = el('div', 'gt-top'), tabs = el('div', 'gt-tabs'), find = el('div', 'gt-find'), page = el('div', 'gt-page'), foot = el('div', 'gt-foot'), grab = el('span', 'gt-grab'), lens = icon('lens');
            grab.setAttribute('aria-hidden', 'true'); foot.setAttribute('aria-hidden', 'true');
            tabs.setAttribute('role', 'tablist'); tabs.setAttribute('aria-label', TEXT.groups);
            TABS.forEach(function (picture, k) {
                var name = k ? T.groups[k - 1].name : TEXT.recent, b = el('button', 'gt-tab');
                b.type = 'button'; b.setAttribute('role', 'tab'); b.setAttribute('aria-label', name); b.setAttribute('title', name); b.appendChild(icon(picture));
                b.addEventListener('pointerdown', stay); b.addEventListener('click', function () { select(k); });
                tabBtns.push(b); tabs.appendChild(b);
            });
            lens.setAttribute('class', 'gt-ico gt-lens');
            q = el('input', 'gt-q'); q.type = 'text'; q.setAttribute('aria-label', TEXT.findLabel); q.setAttribute('placeholder', TEXT.find); q.setAttribute('maxlength', '40');
            q.setAttribute('autocomplete', 'off'); q.setAttribute('autocapitalize', 'none'); q.setAttribute('autocorrect', 'off'); q.setAttribute('spellcheck', 'false'); q.setAttribute('enterkeyhint', 'search');
            clear = el('button', 'gt-x'); clear.type = 'button'; clear.setAttribute('aria-label', TEXT.clear); clear.setAttribute('title', TEXT.clear); clear.hidden = true; clear.appendChild(icon('cross'));
            find.appendChild(lens); find.appendChild(q); find.appendChild(clear);
            top.appendChild(tabs); top.appendChild(find);
            body = el('div', 'gt-body'); body.setAttribute('role', 'tabpanel');
            recentBox = el('div', 'gt-recent'); recentGrid = el('div', 'gt-grid'); recentBox.appendChild(el('p', 'gt-cap', TEXT.recent)); recentBox.appendChild(recentGrid);
            pageCap = el('p', 'gt-cap'); pageCap.setAttribute('role', 'status'); pageGrid = el('div', 'gt-grid'); page.appendChild(pageCap); page.appendChild(pageGrid);
            body.appendChild(recentBox); body.appendChild(page);
            big = el('span', 'gt-big'); nm = el('span', 'gt-nm', TEXT.pickOne); kw = el('span', 'gt-kw'); foot.appendChild(big); foot.appendChild(nm); foot.appendChild(kw);
            pick.appendChild(grab); pick.appendChild(top); pick.appendChild(body); pick.appendChild(foot);
        }
        form.insertBefore(say, form.children[0] || null); if (withEmoji) form.insertBefore(pick, say.nextSibling);
        if (send) form.insertBefore(tools, send); else form.appendChild(tools);
        form.className = String(form.className) + ' gt-on';

        var open = false, byPointer = false, run = null, said = '', gone = 0;
        function note(text) {                                  // the line is a bubble over the log: it goes by itself after a few seconds (not while the microphone listens: that line stays until the state changes), and at once on the next action
            said = text; say.textContent = text; say.hidden = !text;
            if (gone) { clearTimeout(gone); gone = 0; }
            if (text && text !== TEXT.asking && text !== TEXT.listening) gone = setTimeout(function () { gone = 0; if (said === text) note(''); }, BUBBLE);
        }
        field.gtFit = function () {                            // (called by the chat's grow(): the box is ONE row; words that need a second line in the narrow one-row field make it two rows, the class gt-tall; the wide field of two rows then gives the height. The narrow field is always the measure, so the class never flips on a key. An empty field and an off one are one row)
            var s = field.style, h;
            form.classList.remove('gt-tall'); s.height = 'auto'; h = field.scrollHeight;
            if (!field.disabled && field.value !== '' && h > ROW) { form.classList.add('gt-tall'); s.height = 'auto'; h = field.scrollHeight; }
            return h;
        };
        function grown() { if (dock.grow) dock.grow(); }       // (the field is a text area: the chat makes it as tall as its words)
        function sync() { var off = !!field.disabled || !!(dock.off && dock.off()); tools.hidden = off; if (off || form.hidden) { drop(); close(false); } }
        // ---- the picker
        function stay(e) { if (e && e.preventDefault) e.preventDefault(); }       // (a press on a tab, an emoji or the clear button keeps the focus where it is)
        function store() { try { return win.localStorage || null; } catch (e) { return null; } }       // (a browser that refuses its storage keeps the picks for the visit)
        function loadRecent() {                                // what the browser kept: only characters of the table, no twice, at most RECENT (anything else in the stored text is ignored)
            var out = [], s = store(), v = '';
            try { v = s ? String(s.getItem(KEY) || '') : ''; } catch (e) { v = ''; }
            v.slice(0, 120).split(' ').forEach(function (h) { var ch = /^[0-9A-F]{4,5}$/.test(h) ? chr(parseInt(h, 16)) : ''; if (ch && T.by[ch] && out.indexOf(ch) === -1 && out.length < RECENT) out.push(ch); });
            return out;
        }
        function remember(ch) {                                // the pick goes to the front; the list is shown anew the next time the picker opens (an emoji does not move under a finger)
            var s = store();
            if (!recent) recent = loadRecent();
            recent = [ch].concat(recent.filter(function (x) { return x !== ch; })).slice(0, RECENT);
            if (s) try { s.setItem(KEY, recent.map(hex).join(' ')); } catch (e) { /* refused: kept for the visit */ }
        }
        function show(it) { big.textContent = it ? it.ch : ''; nm.textContent = it ? it.name : TEXT.pickOne; kw.textContent = it ? it.keys.join(' / ') : ''; }
        function perRow() {                                    // how many emoji make one row of the grid (the page says; COLS where it cannot)
            var s = win.getComputedStyle && win.getComputedStyle(recentGrid), t = s ? String(s.gridTemplateColumns || '') : '';
            return /^[0-9.]+px( [0-9.]+px)*$/.test(t) ? t.split(' ').length : COLS;
        }
        function stops() { if (at >= cells.length) at = cells.length - 1; if (at < 0) at = 0; cells.forEach(function (b, k) { b.tabIndex = k === at ? 0 : -1; }); }       // (one emoji is in the tab order, the one that the arrow keys left)
        function fill(grid, list) {
            grid.textContent = '';
            list.forEach(function (it) {
                var k = cells.length, b = el('button', 'gt-e', it.ch);
                b.type = 'button'; b.tabIndex = -1; b.setAttribute('aria-label', it.name);
                b.addEventListener('pointerdown', stay); b.addEventListener('click', function () { at = k; choose(it.ch); });
                b.addEventListener('mouseover', function () { show(it); }); b.addEventListener('focus', function () { at = k; show(it); });
                cells.push(b); shown.push(it); grid.appendChild(b);
            });
        }
        function render() {                                    // the page of the picker that the tab, or the search, asks for
            var searching = query !== '', g = T.groups[tab - 1], mine = (recent || []).map(function (ch) { return T.by[ch]; }), list;
            cells = []; shown = [];
            pick.classList[searching ? 'add' : 'remove']('gt-seeking');
            tabBtns.forEach(function (b, k) { b.setAttribute('aria-selected', k === tab ? 'true' : 'false'); b.tabIndex = k === tab ? 0 : -1; b.className = 'gt-tab' + (k === tab && !searching ? ' gt-sel' : ''); });
            clear.hidden = !searching;
            recentBox.hidden = searching || tab === 0 || !mine.length;
            if (searching) { found = search(T, query); list = found.slice(0, SHOWN); pageCap.textContent = !found.length ? TEXT.none : found.length > SHOWN ? 'Showing ' + SHOWN + ' of ' + found.length + ' results. Type more to narrow them down.' : found.length + (found.length === 1 ? ' result' : ' results'); }
            else if (tab === 0) { found = []; list = mine; pageCap.textContent = mine.length ? TEXT.recent : TEXT.noRecent; }
            else { found = []; list = g.items; pageCap.textContent = g.name; }
            if (recentBox.hidden) recentGrid.textContent = ''; else fill(recentGrid, mine.slice(0, perRow()));
            fill(pageGrid, list);
            body.setAttribute('aria-label', searching ? TEXT.findLabel : tab ? g.name : TEXT.recent);
            body.scrollTop = 0; at = 0; stops(); show(null);
        }
        function select(k) { tab = k; query = ''; q.value = ''; render(); }
        function rove(n) { at = n; stops(); if (cells[at] && cells[at].focus) cells[at].focus(); }
        function near(from, dir) {                             // the emoji in the next row up (-1) or down (1) that stands nearest to the one at `from`, or -1 (the rows and places are the page's own)
            var c = cells[from], best = -1, bs = 1e9, k, d, s;
            if (!c || typeof c.offsetTop !== 'number') return -1;
            for (k = 0; k < cells.length; k++) { d = (cells[k].offsetTop - c.offsetTop) * dir; if (d <= 0) continue; s = d * 10000 + Math.abs(cells[k].offsetLeft - c.offsetLeft); if (s < bs) { bs = s; best = k; } }
            return best;
        }
        function ahead(dir, rows) { var to = at, n; while (rows-- > 0 && (n = near(to, dir)) !== -1) to = n; return to; }       // (a page is three rows)
        function modeNow() { var w = win.innerWidth || 1280, h = win.innerHeight || 800; return h <= 500 ? 'low' : w <= 700 ? 'sheet' : 'pop'; }       // (asked when the picker opens and when the width changes: a keyboard that takes the height must not turn a sheet into a box)
        function keepEnd(change) {                             // a log that is read at its end stays at its end while the picker takes room
            var log = dock.root && dock.root.querySelector ? dock.root.querySelector('.gc-log') : null, end = !!log && log.scrollHeight - log.scrollTop - log.clientHeight < 6;
            change();
            if (end) log.scrollTop = log.scrollHeight;
        }
        function sheetFit() {                                  // a phone upright: the picker is a sheet at the bottom (over the place of the keyboard); where the browser says that its keyboard is up (visualViewport), the sheet stands above it and is lower
            var v = win.visualViewport, ih = win.innerHeight || 800, vh = v && v.height ? v.height : ih, lift = v ? Math.max(0, Math.round(ih - vh - (v.offsetTop || 0))) : 0, h = Math.round(Math.min(0.52 * ih, 0.55 * vh)), s = doc.documentElement && doc.documentElement.style;
            pick.style.height = h + 'px'; pick.style.bottom = lift + 'px';
            if (s && s.setProperty) s.setProperty('--gt-sheet', (h + lift) + 'px');
        }
        function unsheet() { var h = doc.documentElement; if (h && h.classList) h.classList.remove('gt-sheet-open'); if (h && h.style && h.style.removeProperty) h.style.removeProperty('--gt-sheet'); }
        function place() {                                     // where the picker stands: a box above the field at the right (a wide window), a row of the window's width above it (a low window: a phone held sideways), or the sheet at the bottom
            var m = mode || modeNow(), s = pick.style, r = form.getBoundingClientRect(), h;
            s.left = ''; s.right = ''; s.top = ''; s.bottom = ''; s.width = ''; s.height = '';
            ['pop', 'low', 'sheet'].forEach(function (k) { pick.classList[k === m ? 'add' : 'remove']('gt-' + k); });
            if (m === 'sheet') { if (doc.documentElement && doc.documentElement.classList) doc.documentElement.classList.add('gt-sheet-open'); sheetFit(); }
            else unsheet();
            if (m === 'pop') { s.height = Math.max(120, Math.min(400, Math.floor(r.top - 20))) + 'px'; }
            else if (m === 'low') { h = Math.max(96, Math.floor(r.top - 12)); s.left = Math.round(r.left) + 'px'; s.width = Math.round(r.width) + 'px'; s.top = '6px'; s.height = h + 'px'; pick.classList[h < 220 ? 'add' : 'remove']('gt-tight'); }
            mode = m;
        }
        function close(back) {
            if (!open) return;
            open = false; pick.hidden = true; emojiBtn.setAttribute('aria-expanded', 'false');
            if (mode === 'sheet') keepEnd(unsheet);
            mode = ''; held = null;
            if (back && field.focus) field.focus();
        }
        function toggle() {
            if (open) { close(true); return; }
            open = true; pick.hidden = false; emojiBtn.setAttribute('aria-expanded', 'true'); if (!run) note('');
            if (!recent) recent = loadRecent();
            query = ''; q.value = '';
            mode = ''; lastW = win.innerWidth; keepEnd(place); render();
            if (byPointer) { if (mode === 'sheet' && doc.activeElement === field && field.blur) field.blur(); }       // (a press: a sheet takes the place of the keyboard; a box leaves the focus, and the keyboard, where they are)
            else if (q.focus) q.focus();                       // (the keyboard: the search field first; Down goes into the emoji)
        }
        function choose(text) {
            var r = insert(field.value, held ? held.a : field.selectionStart, held ? held.b : field.selectionEnd, text, max);       // (a field that has no focus has lost its caret to the press on an emoji: the caret it had when it lost the focus is used)
            if (!r) { note(TEXT.full); return; }
            note(''); field.value = r.value; grown();
            if (field.setSelectionRange) field.setSelectionRange(r.caret, r.caret);
            if (held) held = { a: r.caret, b: r.caret };
            remember(text);
        }
        // ---- voice
        function paint(me, words) { if (micBtn) { micBtn.setAttribute('aria-pressed', run === me && !me.over ? 'true' : 'false'); micBtn.className = 'btn gt-btn gt-mic' + (run === me && !me.over ? ' gt-hot' : ''); } note(words); }
        function draw(me) { var r = speak(me.before, me.text, me.after, max); field.value = r.value; grown(); if (field.setSelectionRange) field.setSelectionRange(r.caret, r.caret); }
        function drop() {                                      // away at once: nothing that the recognizer still says is heard (a send, typing, a page that goes, a field that is off)
            var me = run; if (!me) return;
            run = null; me.over = true; paint(me, '');
            try { me.rec.abort(); } catch (e) { /* it has ended */ }
        }
        function why(code) { return TEXT[policyBlocks(doc) && ERR[code] === 'blocked' ? 'policy' : ERR[code] || 'failed']; }       // (a refusal by the page's own policy says so, not "blocked for this site")
        function finish(me) {
            if (run !== me) return;
            run = null; me.over = true;
            paint(me, me.error ? why(me.error) : me.full ? TEXT.full : (me.text.trim() ? '' : TEXT.nothing));
        }
        function halted(me) { var held = dock.root && dock.root.closest && dock.root.closest('[inert]'); if (held && run === me) drop(); return !!held; }       // (the page is on its way to the game, or a question stands over it: nothing is written, and nothing listens)
        function listen() {
            if (run) { run.over = true; paint(run, ''); try { run.rec.stop(); } catch (e) { drop(); } return; }     // (a second press: what was heard so far stays, and the final words may still come)
            if (policyBlocks(doc)) { note(TEXT.policy); return; }       // (the page's policy refuses the microphone: nothing to ask the browser for)
            var v = field.value, a = typeof field.selectionStart === 'number' ? field.selectionStart : v.length, b = typeof field.selectionEnd === 'number' ? field.selectionEnd : a;
            var me = { rec: null, before: v.slice(0, a), after: v.slice(b), text: '', error: '', over: false, full: false };
            try {
                var rec = new Ctor();
                rec.lang = (win.navigator && win.navigator.language) || 'en-US'; rec.continuous = true; rec.interimResults = true; rec.maxAlternatives = 1;
                rec.onstart = function () { if (halted(me)) return; if (run === me && !me.over) paint(me, TEXT.listening); };
                rec.onresult = function (e) {
                    if (run !== me || halted(me)) return;
                    var t = '', rs = (e && e.results) || [];
                    for (var k = 0; k < rs.length; k++) if (rs[k] && rs[k][0] && typeof rs[k][0].transcript === 'string') t += rs[k][0].transcript + ' ';
                    me.text = t; draw(me);
                    if (points(field.value) >= max - 2 && !me.over) { me.over = true; me.full = true; try { rec.stop(); } catch (x) { /* ended */ } paint(me, TEXT.full); }
                };
                rec.onerror = function (e) { if (run === me && e && e.error !== 'aborted') { me.error = String(e.error || 'failed'); note(why(me.error)); } };       // (said at once: some phones end the listening late)
                rec.onend = function () { finish(me); };
                me.rec = rec; run = me; paint(me, TEXT.asking);
                rec.start();
            } catch (e) { if (run === me) run = null; me.over = true; paint(me, TEXT.failed); }
        }
        function hold(e) { byPointer = true; if (e && e.preventDefault) e.preventDefault(); }       // (a press with a mouse or a finger keeps the focus where it is: the phone's keyboard stays up, the field keeps its caret)
        emojiBtn.addEventListener('pointerdown', hold);
        emojiBtn.addEventListener('click', function () { toggle(); byPointer = false; });
        if (micBtn) micBtn.addEventListener('click', function () { if (open) close(false); listen(); });
        if (withEmoji) {
            clear.addEventListener('pointerdown', stay);
            clear.addEventListener('click', function () { q.value = ''; query = ''; render(); if (q.focus) q.focus(); });
            q.addEventListener('input', function () { query = q.value; render(); });
            q.addEventListener('keydown', function (e) {       // (Enter is the search's: it must not send the chat line that the form would send)
                var k = e && e.key;
                if (k === 'Enter') { if (e.preventDefault) e.preventDefault(); if (found.length) choose(found[0].ch); }
                else if (k === 'ArrowDown' && cells.length) { if (e.preventDefault) e.preventDefault(); rove(0); }
            });
            tabs.addEventListener('keydown', function (e) {
                var k = e && e.key, to = k === 'ArrowRight' ? tab + 1 : k === 'ArrowLeft' ? tab - 1 : k === 'Home' ? 0 : k === 'End' ? TABS.length - 1 : -1;
                if (to === -1) return;
                if (e.preventDefault) e.preventDefault();
                select(Math.max(0, Math.min(TABS.length - 1, to)));
                if (tabBtns[tab].focus) tabBtns[tab].focus();
            });
            body.addEventListener('keydown', function (e) {    // (the arrow keys move between the emoji: left and right one by one, up and down by row, Home and End to the ends, a page is three rows)
                var k = e && e.key, to = -1;
                if (k === 'ArrowRight') to = at + 1; else if (k === 'ArrowLeft') to = at - 1; else if (k === 'ArrowDown') to = near(at, 1);
                else if (k === 'ArrowUp') { to = near(at, -1); if (to === -1) { if (e.preventDefault) e.preventDefault(); if (q.focus) q.focus(); return; } }
                else if (k === 'PageDown') to = ahead(1, 3); else if (k === 'PageUp') to = ahead(-1, 3);
                else if (k === 'Home') to = 0; else if (k === 'End') to = cells.length - 1;
                else return;
                if (e.preventDefault) e.preventDefault();
                if (to >= 0 && to < cells.length) rove(to);
            });
            pick.addEventListener('mouseleave', function () { show(doc.activeElement === cells[at] ? shown[at] : null); });
            field.addEventListener('blur', function () { if (open) held = { a: field.selectionStart, b: field.selectionEnd }; });       // (the picker is open and the focus goes to the search field or nowhere: the caret is kept)
            field.addEventListener('focus', function () { held = null; if (open && mode === 'sheet') close(false); });       // (the keyboard comes for the field: the sheet gives its place)
            if (win.visualViewport && win.visualViewport.addEventListener) { var follow = function () { if (open && mode === 'sheet') keepEnd(sheetFit); }; win.visualViewport.addEventListener('resize', follow); win.visualViewport.addEventListener('scroll', follow); }
        }
        form.addEventListener('keydown', function (e) {        // (before the dock's own Escape, which folds the panel: the picker, or the listening, is closed first)
            if (!e || e.key !== 'Escape' || !(open || run)) return;
            if (e.stopPropagation) e.stopPropagation();
            if (open) close(true); else listen();
        });
        form.addEventListener('submit', function () { drop(); close(false); note(''); });
        field.addEventListener('input', function () { if (run) drop(); else note(''); });
        doc.addEventListener('visibilitychange', function () { if (doc.hidden) { drop(); close(false); } });
        doc.addEventListener('pointerdown', function (e) { if (open && e && e.target && !form.contains(e.target)) close(false); });
        win.addEventListener('pagehide', function () { drop(); });
        win.addEventListener('resize', function () { if (!open) return; var was = mode; if (win.innerWidth !== lastW) { lastW = win.innerWidth; mode = ''; } keepEnd(place); if (mode !== was) render(); });
        if (dock.tap) dock.tap(function () { sync(); });
        sync();
        return { root: tools, pick: pick, say: say, emoji: emojiBtn, mic: micBtn, field: field, drop: drop, close: close };
    }

    return { LIMIT: LIMIT, TEXT: TEXT, ERR: ERR, GROUPS: GROUPS, emoji: emoji, table: table, search: search, points: points, head: head, insert: insert, speak: speak, recognition: recognition, policyBlocks: policyBlocks, mount: mount };
});
