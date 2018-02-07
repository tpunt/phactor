--TEST--
Self-sending messages via current instance.
--FILE--
<?php

use phactor\{ActorSystem, Actor, function spawn};

$actorSystem = new ActorSystem(true);

class Test extends Actor
{
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
            ActorSystem::shutdown();
        }
    }
}

spawn('test', Test::class);
--EXPECT--
int(1000)
