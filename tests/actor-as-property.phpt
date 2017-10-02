--TEST--
Allow for Actors to be properties
--DESCRIPTION--
Allow for Actor-based objects to be assigned as properties of other Actor-based
objects.
--FILE--
<?php

$actorSystem = new ActorSystem();

class Test extends Actor
{
    public function receive($sender, $message){}
}

new class(new Test) extends Actor {
    private $actor;

    public function __construct(Actor $actor)
    {
        $this->actor = $actor;
        var_dump($this->actor);
        $this->send($this, 0);
    }

    public function receive($sender, $message)
    {
        var_dump($this->actor);
    }
};

$actorSystem->block();
--EXPECTF--
object(Test)#%d (0) {
}
object(Test)#%d (0) {
}
