<?php

$actorSystem = new ActorSystem();

new class extends Actor {
    public function __construct()
    {
        $this->send($this, 1);
    }

    public function receive($sender, $message)
    {
        var_dump($message);

        if ($message < 10) {
            $this->send($this, $message + 1);
        }
    }
};

$actorSystem->block();
