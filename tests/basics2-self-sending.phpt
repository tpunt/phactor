--TEST--
Basics 1 test - ping pong
--DESCRIPTION--
Test basic message passing
--FILE--
<?php

$actorSystem = new ActorSystem(true);

spawn('test', Test::class);

class Test extends Actor
{
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        var_dump($message);

        if ($message < 10) {
            $this->send($this, $message + 1);
        } else {
            ActorSystem::shutdown();
        }
    }
}

$actorSystem->block();
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
