--TEST--
Self-sending messages via current instance.
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem();

class Test extends Actor
{
    private $counter = 1;
    const MAX = 1000;

    public function __construct()
    {
        $ar = ActorRef::fromActor($this);

        for ($i = 0; $i < self::MAX; ++$i) {
            $this->send($ar, 1);
        }
    }

    public function receive()
    {
        while ($this->counter !== self::MAX) {
            $this->counter += $this->receiveBlock();
        }

        var_dump($this->counter);
        ActorSystem::shutdown();
    }
}

new ActorRef(Test::class);
--EXPECT--
int(1000)
