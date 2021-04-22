# Poacher detector

This repository contains code that aims to prevent poaching of rhinos by detecing humans in a savanna environment.

Two pipelines have been implemented because of the uncertainty of a reliable internet connection.

## Highend pipeline

The `pipeline_highend` assumes a better internet connection, sending all images where motion has been observed.

## Lowend pipeline

The `pipeline_lowend` is implemented with the goal of reducing internet traffic. This is done by only sending an image over the internet when there has been two human classifications within a certain time frame.
