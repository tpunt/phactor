--TEST--
Ensure property mutation via compound assignment operators works.
--DESCRIPTION--
This ensures that compound assignment still works (this required not using the
default get_property_ptr_ptr handler). Quite strangely, though, enabling OPcache
(specifically its optimisation level 1024) also fixed this issue.
--FILE--
<?php

$actorSystem = new ActorSystem();

new class extends Actor {
    private $a = 1;

    public function __construct()
    {
        $this->a += 1;
        var_dump($this->a);
        $this->send($this, 1);
    }
    public function receive($sender, $message)
    {
        $this->a += $message;
        var_dump($this->a);
    }
};

$actorSystem->block();
--EXPECT--
int(2)
int(3)
