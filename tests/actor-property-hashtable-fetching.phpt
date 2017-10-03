--TEST--
Fetching the hash table of an Actor (specifically with respect to the cyclic
garbage collector)
--DESCRIPTION--
Since an Actor object is reused by multiple threads, we cannot safely use the
properties table. If we were to use it, it would need to be allocated by the
thread creating the Actor object.

But this will not be enough, since inserting new values may mean strings (for
property names) will be allocated by other threads, leading to heap corruption
issues. (There could also be race conditions if the GC traverses the hash table
in one thread whilst it is being updated by another thread). So the solution is
to simply regenerate a new table each time all of the properties are fetched
(which should be done rarely).

We also override the `get_gc` handler because there is no need to build a hash
table of values from scratch, only to see if any need to be freed.
--FILE--
<?php

$actorSystem = new ActorSystem();

$actor = new class extends Actor {
    private $counter = 1;
    const MAX = 1000;

    public function __construct()
    {
        for ($i = 0; $i < self::MAX; ++$i) {
            $this->send($this, 1);
        }
    }

    public function receive($sender, $message)
    {
        if (($this->counter += $message) === self::MAX) {
            var_dump($this->counter);
        }
    }
};

$actorSystem->block();
--EXPECTF--
int(1000)
