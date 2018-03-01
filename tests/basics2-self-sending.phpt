--TEST--
Basics 1 test - ping pong
--DESCRIPTION--
Test basic message passing
--FILE--
<?php

use phactor\{ActorSystem, Actor, ActorRef};

$actorSystem = new ActorSystem(true);

new ActorRef(Test::class, [], 'test');

class Test extends Actor
{
    public function __construct()
    {
        $ar = ActorRef::fromActor($this);
        $this->send($ar, [$ar, 1]);
    }

    public function receive()
    {
        while (true) {
            [$sender, $message] = $this->receiveBlock();

            var_dump($message);

            if ($message < 10) {
                $this->send($sender, [$sender, $message + 1]);
            } else {
                break;
            }
        }
        ActorSystem::shutdown();
    }
}
--EXPECT--
int(1)
int(2)
int(3)
int(4)
int(5)
int(6)
int(7)
int(8)
int(9)
int(10)
