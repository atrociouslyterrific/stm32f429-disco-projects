// The rules of the tank: a small ecosystem.
//
//   Plants grow on the seafloor, faster when the water is rich in nutrients. Grown plants
//   release food into the water. Small fish can also nibble the plants themselves.
//   Everyone starts as a tiny Fry. Eating makes an animal grow, and after enough meals it
//   evolves into the next kind of sea animal: Fry, Squid, Turtle, then either Shark and
//   Orca (if it has been a hunter) or Ray and Whale (if it has been gentle).
//   Hungry animals hunt weaker ones; enemies fight. Every bite is also a meal.
//   When a fish dies, what is left of it becomes nutrients, which feed the plants.
//
// Every fish is steered by its own small neural-network brain. There is no screen code
// in here, so the very same file runs on the board (to show the tank), in web browsers
// (the website's tank) and on GitHub (to train the brains at high speed).
#ifndef FISH_SIM_H
#define FISH_SIM_H

#include <stdint.h>

#define TANK_W 240.0f
#define TANK_H 224.0f   // the rest of the screen shows each fish's health and hunger
#define FLOOR_H 14.0f   // the seafloor is this thick, at the bottom of the tank
#define PLANT_TALL 60.0f // a fully grown plant stands this high

// Rocks the animals must swim around: centre x, centre y, radius (the same on the board,
// the website and in training).
#define N_OBSTACLES 3
extern const float OBSTACLES[N_OBSTACLES][3];

// One day in the tank lasts 3 minutes (5400 ticks). See Tank.light.
#define DAY_TICKS 5400

#define N_FISH 10
#define N_FOOD 8
#define N_PLANTS 6
#define N_STAGES 5 // steps of evolution, 0 to 4

// The kinds of sea animal. Everyone starts as a Fry; see the top of this file for the order.
enum { K_FRY, K_SQUID, K_TURTLE, K_SHARK, K_RAY, K_ORCA, K_WHALE, N_KINDS };
#define NOBODY N_FISH

// Each animal's brain is a neural network with two hidden layers:
//   29 senses -> 16 cells -> 8 cells -> 3 decisions (turn, how hard to swim, whether to bite)
// The senses include its own personality traits, so animals with the same brain still
// behave differently. The brain is nothing but a list of numbers (weights): different
// numbers, different animal.
#define N_IN  29
#define N_H1  16
#define N_H2  8
#define N_OUT 3
// Layer by layer: each first-layer cell has a weight per sense (the "always on" sense
// gives it a resting level); each second-layer cell a weight per first-layer cell plus a
// resting level; each decision a weight per second-layer cell plus a resting level.
#define N_WEIGHTS (N_H1 * N_IN + N_H2 * (N_H1 + 1) + N_OUT * (N_H2 + 1))

typedef struct {
  float w[N_WEIGHTS];
} Brain;

#define VOICE_ACCENT 8

// Genetics. Every animal carries a genome: a set of genes it was born with and keeps for
// life. A baby gets each gene from one of its two parents (chosen at random), and now and
// then a gene changes a little on the way (a mutation). So families look and behave alike,
// and the tank slowly drifts towards whatever genes help animals survive and have babies.
// The brain is part of the genome too: its 200-odd connection strengths are genes, passed
// down in blocks (one block per hidden cell), like chromosomes.
typedef struct {
  float pace;      // swimming speed           0.85 .. 1.25
  float slim;      // body shape: above 1 long and thin, below 1 short and round   0.8 .. 1.25
  float bulk;      // build: how big it can grow at each step (and so its health and bite),
                   // but bigger bodies burn more energy   0.85 .. 1.3
  float thrift;    // burns less energy, but gets less growth out of each meal     0.8 .. 1.2
  // Personality traits, 0 .. 1 each:
  float boldness;    // how soon hunger turns it into a hunter
  float sociability; // how much it wants company: friendships grow faster, schooling pays more
  float aggression;  // how small a grudge it takes to start a fight
  float loyalty;     // loyal animals never attack their own family, and take it hard when
                     // a friend or relative is attacked
  float curiosity;   // curious animals wander off the straight path, exploring
  // Voice: how it talks (used by Fish/fish_talk.c). Inherited like every other gene, so
  // families drift into dialects of their own.
  float chattiness;  // how often it starts a conversation, or keeps one going   0 .. 1
  float creativity;  // how adventurous its word choices are                     0 .. 1
  float exclaim;     // fondness for "!"                                         0 .. 1
  float question;    // fondness for "?"                                         0 .. 1
  float brevity;     // short lines (1) or long ones (0)                         0 .. 1
  float accent[VOICE_ACCENT]; // its own lean in which words it picks, -1 .. 1 each
  uint8_t r, g, b; // its colour
  uint8_t marking; // its pattern: 0 plain, 1 a stripe across the middle, 2 a dark back
} Genome;
#define N_BODY_GENES 11 // pace, slim, bulk, thrift, the five traits, colour, pattern

// The colours of the ten founders: the animals the tank starts with.
extern const uint8_t FOUNDER_COLOURS[N_FISH][3];

typedef struct {
  float x, y;     // where it is
  float dx, dy;   // which way it faces (a direction of length 1)
  float speed;
  float energy;   // 1 = full, 0 = empty. Hunger is 1 - energy.
  float health;   // 0 = dead. How much it can have depends on its size (see fish_max_health):
                  // bigger fish take more killing. Falls when starving or bitten, heals when fed.
  float size;     // how big it is right now (1 = average). The more it eats, the bigger it gets.
  float growth;   // "meal points" earned by eating; enough of them and it evolves
  float max_size; // the biggest its current species can be

  Genome g;       // what it was born with (see above)

  // Its family. Every animal has its own number; the founders have none for parents.
  uint16_t id, mum, dad; // 0 = no parent (a founder)
  uint16_t gen;          // generation: founders are 0, their babies 1, and so on
  uint8_t family;        // which founder's family line it belongs to (its mum's), 0 .. 9

  float score;    // how well it is doing; decides who has babies
  uint16_t eaten, wall_hits, bites, kills;
  uint16_t age;        // ticks alive
  uint8_t stage;       // how many times it has evolved, 0 to 4
  uint8_t kind;        // which sea animal it is now (K_FRY ... K_WHALE)
  uint8_t bite_wait;   // ticks until it can bite again
  uint8_t dead;        // set when its health runs out; whoever runs the tank replaces it
  uint8_t eaten_by;    // which fish finished it off, or NOBODY if it starved
  // A tip it was given ("follow me!", "try the plants"): where food is, remembered for a while.
  float tip_x, tip_y;
  uint16_t tip_ticks;
  Brain brain;
} Fish;

typedef struct {
  float x, y;
  uint8_t there;
} Food;

typedef struct {
  float x;      // where on the seafloor it grows
  float height; // 0 = nibbled to the ground, 1 = fully grown
} Plant;

// Things worth telling someone about. a and b are fish numbers unless it says otherwise.
enum {
  EV_BIT,     // a bit b (a smaller fish it was hunting)
  EV_ATE,     // a ate b
  EV_STARVED, // a starved
  EV_BORN,    // a was born, the baby of b and c
  EV_LIKES,   // a has come to like b
  EV_HATES,   // a has come to hate b
  EV_FOUGHT,  // a attacked its enemy b, and b fought back
  EV_EVOLVED, // a evolved; b is the kind of animal it became
};
typedef struct {
  uint8_t type, a, b, c; // c: for EV_BORN, the second parent
} Event;
#define MAX_EVENTS 24

typedef struct {
  Fish fish[N_FISH];
  Food food[N_FOOD];
  Plant plants[N_PLANTS];
  float nutrients; // how rich the water is, 0..1. Dead fish add to it; plants use it up.
  // The environment:
  float light;            // 1 = midday, 0.25 = darkest night. Animals see less far at night,
                          // and plants only grow in the light.
  float current_x, current_y; // the water's drift, slowly turning; carries animals and food
  // Relationships: like[a][b] is how fish a feels about fish b,
  // from -1 (enemy) through 0 (stranger) to +1 (friend). It needn't be mutual.
  float like[N_FISH][N_FISH];
  uint8_t said[N_FISH][N_FISH]; // 1 = "a likes b" has been reported, 2 = "a hates b" (so it isn't repeated)
  uint32_t rng;
  uint32_t step;
  uint16_t next_id; // the number the next animal born will get
  uint32_t total_eaten;  // food pellets eaten
  uint32_t total_killed; // fish eaten by other fish
  uint32_t total_starved;
  // A look inside the brains, for anyone who wants to watch them think: what each animal
  // sensed on the latest tick, what its hidden cells made of that, and what it decided.
  float seen[N_FISH][N_IN], thought[N_FISH][N_H1], thought2[N_FISH][N_H2], decided[N_FISH][N_OUT];
  // What happened during the latest tick (plus any babies born since). Emptied every tick.
  Event events[MAX_EVENTS];
  uint8_t n_events;
} Tank;

// Random numbers (the same sequence for the same starting seed).
uint32_t rng_next(uint32_t *state);
float rng_unit(uint32_t *state);   // between 0 and 1
float rng_normal(uint32_t *state); // mostly near 0, sometimes further out

// A hand-written starter brain: basic instincts. Training starts from this and improves it.
void brain_instinct(Brain *brain);

// Copy a parent's brain with small random changes. amount = how big the changes are.
void brain_child(Brain *child, const Brain *parent, uint32_t *rng, float amount);

void fish_random_genes(Fish *fish, uint32_t *rng); // (keeps its colour and pattern)
float fish_power(const Fish *fish); // how strong it is in a fight: size and species both count
float fish_max_health(const Fish *fish); // bigger fish have more health...
float fish_bite(const Fish *fish);       // ...and a harder bite; smaller fish, less of both
#define HEALTH_LIMIT 3.0f // the most health any animal can have (a full-grown, big-boned Whale)
void tank_init(Tank *tank, uint32_t seed); // everyone starts as a Fry
// For training only: turn the tank into a going concern, with animals at every step of
// evolution, so brains get practice at being (and meeting) all of them.
void tank_scatter_stages(Tank *tank);
int tank_drop_food(Tank *tank);                   // at a random spot; returns the slot or -1
int tank_add_food(Tank *tank, float x, float y);  // at a chosen spot
// Bring fish i back as a newborn Fry somewhere random, forgetting its relationships.
// Its brain and genes are left alone; change them afterwards if it should be a new fish.
void tank_rebirth(Tank *tank, int i);
// Animal `gone` has died: one of the animals doing best has a baby in its place with a partner
// (its best friend, or failing that the next best animal). The baby's genome is a mix of
// both parents' with a few mutations. Parents and baby start out as friends.
// Returns the first parent (the mum).
int tank_replace(Tank *tank, int gone);
void tank_step(Tank *tank); // move time forward one tick

// Animal `listener` heard `speaker` say `text`. Words matter: kind ones make the listener
// like the speaker more, hostile ones less, and talk of food tells the listener where food
// is. Returns what it did (HEARD_...), for showing.
enum { HEARD_NOTHING, HEARD_KIND, HEARD_HOSTILE, HEARD_FOOD_TIP };
int tank_hear(Tank *tank, int speaker, int listener, const char *text);

#endif
