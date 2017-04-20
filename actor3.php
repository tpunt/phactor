<?php

$actorSystem = new ActorSystem();

new class extends Actor {
    private $arr = [];

    public function __construct()
    {
        $this->send($this, $this->arr);
    }

    public function receive($sender, $message)
    {
        var_dump($this->arr);
        // for ($i = 0; $i < 1; ++$i) $this->arr[] = $i;
        // if (count($message) === 3) {
            // var_dump(array_shift($message));
            // $this->send($this, $message);
        // } else {
            // var_dump($message, $this->arr);
        // }
    }
};

$actorSystem->block();
